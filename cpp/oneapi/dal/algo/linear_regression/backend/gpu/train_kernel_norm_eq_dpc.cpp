/*******************************************************************************
* Copyright 2021 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "oneapi/dal/detail/profiler.hpp"

#include "oneapi/dal/detail/common.hpp"
#include "oneapi/dal/backend/interop/common.hpp"
#include "oneapi/dal/backend/interop/common_dpc.hpp"
#include "oneapi/dal/backend/interop/error_converter.hpp"
#include "oneapi/dal/backend/interop/table_conversion.hpp"

#include "oneapi/dal/backend/dispatcher.hpp"
#include "oneapi/dal/backend/primitives/ndarray.hpp"
#include "oneapi/dal/backend/primitives/lapack.hpp"
#include "oneapi/dal/backend/primitives/utils.hpp"

#include "oneapi/dal/table/row_accessor.hpp"

#include "oneapi/dal/algo/linear_regression/common.hpp"
#include "oneapi/dal/algo/linear_regression/train_types.hpp"
#include "oneapi/dal/algo/linear_regression/backend/model_impl.hpp"
#include "oneapi/dal/algo/linear_regression/backend/gpu/train_kernel.hpp"
#include "oneapi/dal/algo/linear_regression/backend/gpu/update_kernel.hpp"

namespace oneapi::dal::linear_regression::backend {

using daal::services::Status;
using dal::backend::context_gpu;

namespace be = dal::backend;
namespace pr = be::primitives;
namespace interop = dal::backend::interop;

template <typename Float>
std::int64_t propose_block_size(const sycl::queue& q, const std::int64_t f, const std::int64_t r) {
    constexpr std::int64_t fsize = sizeof(Float);
    return 0x10000l * (8 / fsize);
}

template <typename Float, typename Task>
static train_result<Task> call_dal_kernel(const context_gpu& ctx,
                                          const detail::descriptor_base<Task>& desc,
                                          const table& data,
                                          const table& resp) {
    using dal::detail::check_mul_overflow;

    using model_t = model<Task>;
    using model_impl_t = detail::model_impl<Task>;

    auto& queue = ctx.get_queue();
    interop::execution_context_guard guard(queue);
    ONEDAL_PROFILER_TASK(linreg_train_kernel, queue);

    constexpr auto uplo = pr::mkl::uplo::upper;
    constexpr auto alloc = sycl::usm::alloc::device;

    row_accessor<const Float> x_accessor(data);
    row_accessor<const Float> y_accessor(resp);

    const auto sample_count = data.get_row_count();
    const auto feature_count = data.get_column_count();
    const auto response_count = resp.get_column_count();
    ONEDAL_ASSERT(sample_count == resp.get_row_count());
    const bool beta = desc.get_compute_intercept();
    const std::int64_t ext_feature_count = feature_count + beta;

    const auto betas_size = check_mul_overflow(response_count, feature_count + 1);
    auto betas_arr = array<Float>::zeros(queue, betas_size, alloc);

    const auto b_count = propose_block_size<Float>(queue, feature_count, response_count);
    const be::uniform_blocking blocking(sample_count, b_count);

    const pr::ndshape<2> xty_shape{ response_count, ext_feature_count };
    const pr::ndshape<2> betas_shape{ response_count, feature_count + 1 };
    const pr::ndshape<2> xtx_shape{ ext_feature_count, ext_feature_count };

    auto [xty, fill_xty_event] =
        pr::ndarray<Float, 2, pr::ndorder::f>::zeros(queue, xty_shape, alloc);
    auto [xtx, fill_xtx_event] =
        pr::ndarray<Float, 2, pr::ndorder::c>::zeros(queue, xtx_shape, alloc);
    sycl::event last_xty_event = fill_xty_event, last_xtx_event = fill_xtx_event;

    for (std::int64_t b = 0; b < blocking.get_block_count(); ++b) {
        const auto last = blocking.get_block_end_index(b);
        const auto first = blocking.get_block_start_index(b);

        const std::int64_t length = last - first;

        auto x_arr = x_accessor.pull(queue, { first, last }, alloc);
        auto x = pr::ndarray<Float, 2>::wrap(x_arr, { length, feature_count });

        auto y_arr = y_accessor.pull(queue, { first, last }, alloc);
        auto y = pr::ndarray<Float, 2>::wrap(y_arr, { length, response_count });

        last_xty_event = update_xty(queue, beta, x, y, xty, { last_xty_event });
        last_xtx_event = update_xtx(queue, beta, x, xtx, { last_xtx_event });
    }

    const be::event_vector solve_deps{ last_xty_event, last_xtx_event };

    auto& comm = ctx.get_communicator();
    if (comm.get_rank_count() > 1) {
        sycl::event::wait_and_throw(solve_deps);
        {
            ONEDAL_PROFILER_TASK(xtx_allreduce);
            auto xtx_arr = dal::array<Float>::wrap(queue, xtx.get_mutable_data(), xtx.get_count());
            comm.allreduce(xtx_arr).wait();
        }
        {
            ONEDAL_PROFILER_TASK(xty_allreduce);
            auto xty_arr = dal::array<Float>::wrap(queue, xty.get_mutable_data(), xty.get_count());
            comm.allreduce(xty_arr).wait();
        }
    }

    auto nxtx = pr::ndarray<Float, 2>::empty(queue, xtx_shape, alloc);
    auto nxty = pr::ndarray<Float, 2>::wrap_mutable(betas_arr, betas_shape);
    auto solve_event = pr::solve_system<uplo>(queue, beta, xtx, xty, nxtx, nxty, solve_deps);
    sycl::event::wait_and_throw({ solve_event });

    auto betas = homogen_table::wrap(betas_arr, response_count, feature_count + 1);

    const auto model_impl = std::make_shared<model_impl_t>(betas);
    const auto model = dal::detail::make_private<model_t>(model_impl);

    const auto options = desc.get_result_options();
    auto result = train_result<Task>().set_model(model).set_result_options(options);

    if (options.test(result_options::intercept)) {
        auto arr = array<Float>::zeros(queue, response_count, alloc);
        auto dst = pr::ndarray<Float, 2>::wrap_mutable(arr, { 1l, response_count });
        const auto src = nxty.get_col_slice(0l, 1l).t();

        pr::copy(queue, dst, src).wait_and_throw();

        auto intercept = homogen_table::wrap(arr, 1l, response_count);
        result.set_intercept(intercept);
    }

    if (options.test(result_options::coefficients)) {
        const auto size = check_mul_overflow(response_count, feature_count);

        auto arr = array<Float>::zeros(queue, size, alloc);
        const auto src = nxty.get_col_slice(1l, feature_count + 1);
        auto dst = pr::ndarray<Float, 2>::wrap_mutable(arr, { response_count, feature_count });

        pr::copy(queue, dst, src).wait_and_throw();

        auto coefficients = homogen_table::wrap(arr, response_count, feature_count);
        result.set_coefficients(coefficients);
    }

    return result;
}

template <typename Float, typename Task>
static train_result<Task> train(const context_gpu& ctx,
                                const detail::descriptor_base<Task>& desc,
                                const train_input<Task>& input) {
    return call_dal_kernel<Float, Task>(ctx, desc, input.get_data(), input.get_responses());
}

template <typename Float, typename Task>
struct train_kernel_gpu<Float, method::norm_eq, Task> {
    train_result<Task> operator()(const context_gpu& ctx,
                                  const detail::descriptor_base<Task>& desc,
                                  const train_input<Task>& input) const {
        return train<Float, Task>(ctx, desc, input);
    }
};

template struct train_kernel_gpu<float, method::norm_eq, task::regression>;
template struct train_kernel_gpu<double, method::norm_eq, task::regression>;

} // namespace oneapi::dal::linear_regression::backend
