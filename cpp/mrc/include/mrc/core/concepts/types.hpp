/**
 * SPDX-FileCopyrightText: Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "mrc/core/concepts/eval.hpp"

#include <concepts>

namespace mrc::core::concepts {

template <typename T>
concept not_void = requires { not std::same_as<T, void>; };

template <typename T>
concept data = std::movable<T>;

template <typename T>
concept has_data_type = data<typename T::data_type>;

template <typename T, typename DataT>
concept has_data_type_of = has_data_type<T> && std::same_as<typename T::data_type, DataT>;

template <typename T, auto ConceptFn>
concept has_data_type_of_concept = has_data_type<T> && eval_concept_fn_v<ConceptFn, typename T::data_type>;

}  // namespace mrc::core::concepts
