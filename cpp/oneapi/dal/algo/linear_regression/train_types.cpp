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

#include "oneapi/dal/algo/linear_regression/train_types.hpp"
#include "oneapi/dal/detail/common.hpp"
#include "oneapi/dal/exceptions.hpp"

namespace oneapi::dal::linear_regression {

template <typename Task>
class detail::v1::train_input_impl : public base {
public:
    train_input_impl(const table& data, const table& responses = table{})
            : data(data),
              responses(responses) {}

    table data;
    table responses;
};

template <typename Task>
class detail::v1::train_result_impl : public base {
public:
    table intercept;
    table coefficients;

    result_option_id options;

    model<Task> trained_model;
};

using detail::v1::train_input_impl;
using detail::v1::train_result_impl;

namespace v1 {

template <typename Task>
train_input<Task>::train_input(const table& data, const table& responses)
        : impl_(new train_input_impl<Task>(data, responses)) {}

template <typename Task>
train_input<Task>::train_input(const table& data) : impl_(new train_input_impl<Task>(data)) {}

template <typename Task>
const table& train_input<Task>::get_data() const {
    return impl_->data;
}

template <typename Task>
const table& train_input<Task>::get_responses() const {
    return impl_->responses;
}

template <typename Task>
void train_input<Task>::set_data_impl(const table& value) {
    impl_->data = value;
}

template <typename Task>
void train_input<Task>::set_responses_impl(const table& value) {
    impl_->responses = value;
}

template <typename Task>
train_result<Task>::train_result() : impl_(new train_result_impl<Task>{}) {}

template <typename Task>
const model<Task>& train_result<Task>::get_model() const {
    return impl_->trained_model;
}

template <typename Task>
void train_result<Task>::set_model_impl(const model<Task>& value) {
    impl_->trained_model = value;
}

template <typename Task>
const table& train_result<Task>::get_intercept() const {
    using msg = dal::detail::error_messages;
    if (!get_result_options().test(result_options::intercept)) {
        throw domain_error(msg::this_result_is_not_enabled_via_result_options());
    }
    return impl_->intercept;
}

template <typename Task>
void train_result<Task>::set_intercept_impl(const table& value) {
    using msg = dal::detail::error_messages;
    if (!get_result_options().test(result_options::intercept)) {
        throw domain_error(msg::this_result_is_not_enabled_via_result_options());
    }
    impl_->intercept = value;
}

template <typename Task>
const table& train_result<Task>::get_coefficients() const {
    using msg = dal::detail::error_messages;
    if (!get_result_options().test(result_options::coefficients)) {
        throw domain_error(msg::this_result_is_not_enabled_via_result_options());
    }
    return impl_->coefficients;
}

template <typename Task>
void train_result<Task>::set_coefficients_impl(const table& value) {
    using msg = dal::detail::error_messages;
    if (!get_result_options().test(result_options::coefficients)) {
        throw domain_error(msg::this_result_is_not_enabled_via_result_options());
    }
    impl_->coefficients = value;
}

template <typename Task>
const table& train_result<Task>::get_packed_coefficients() const {
    return impl_->trained_model.get_packed_coefficients();
}

template <typename Task>
void train_result<Task>::set_packed_coefficients_impl(const table& value) {
    impl_->trained_model.set_packed_coefficients(value);
}

template <typename Task>
const result_option_id& train_result<Task>::get_result_options() const {
    return impl_->options;
}

template <typename Task>
void train_result<Task>::set_result_options_impl(const result_option_id& value) {
    impl_->options = value;
}

template class ONEDAL_EXPORT train_input<task::regression>;
template class ONEDAL_EXPORT train_result<task::regression>;

} // namespace v1
} // namespace oneapi::dal::linear_regression
