/*******************************************************************************
* Copyright 2022 Intel Corporation
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

#pragma once

#include "oneapi/dal/backend/primitives/ndarray.hpp"
#include "oneapi/dal/backend/primitives/ndindexer.hpp"

namespace oneapi::dal::backend::primitives {

#ifdef ONEDAL_DATA_PARALLEL
template <typename Functor,
          typename Input1Type,
          typename Input2Type,
          typename OutputType,
          ndorder input1_layout,
          ndorder input2_layout,
          ndorder output_layout>
inline sycl::event element_wise(sycl::queue queue,
                                const Functor& functor,
                                const ndview<Input1Type, 2, input1_layout>& input1,
                                const ndview<Input2Type, 2, input2_layout>& input2,
                                ndview<OutputType, 2, output_layout>& output,
                                const event_vector& deps = {}) {
    const auto shape = output.get_shape();
    ONEDAL_ASSERT(shape == input1.get_shape());
    ONEDAL_ASSERT(shape == input2.get_shape());

    auto out = make_ndindexer(output);
    auto inp1 = make_ndindexer(input1);
    auto inp2 = make_ndindexer(input2);

    return queue.submit([&](sycl::handler& h) {
        h.depends_on(deps);

        const auto range = shape.to_range();
        h.parallel_for(range, [=](sycl::id<2> idx) {
            const auto& l = inp1.at(idx[0], idx[1]);
            const auto& r = inp2.at(idx[0], idx[1]);
            out.at(idx[0], idx[1]) = functor(l, r);
        });
    });
}

template <typename Functor,
          typename Input1Type,
          typename Input2Type,
          typename OutputType,
          ndorder input_layout,
          ndorder output_layout>
inline sycl::event element_wise(sycl::queue queue,
                                const Functor& functor,
                                const ndview<Input1Type, 2, input_layout>& input,
                                const Input2Type& argument,
                                ndview<OutputType, 2, output_layout>& output,
                                const event_vector& deps = {}) {
    const auto shape = output.get_shape();
    ONEDAL_ASSERT(shape == input.get_shape());

    auto out = make_ndindexer(output);
    auto inp = make_ndindexer(input);

    return queue.submit([&](sycl::handler& h) {
        h.depends_on(deps);

        const auto range = shape.to_range();
        h.parallel_for(range, [=](sycl::id<2> idx) {
            const auto& l = inp.at(idx[0], idx[1]);
            out.at(idx[0], idx[1]) = functor(l, argument);
        });
    });
}
#endif

} // namespace oneapi::dal::backend::primitives
