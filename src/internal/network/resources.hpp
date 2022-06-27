/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "internal/resources/partition_resources_base.hpp"
#include "internal/ucx/registration_cache.hpp"

#include <cstddef>
#include <memory>

namespace srf::internal::ucx {
class Resources;
}  // namespace srf::internal::ucx

namespace srf::internal::network {

class Resources final : private resources::PartitionResourceBase
{
  public:
    Resources(runnable::Resources& _runnable_resources, std::size_t _partition_id, ucx::Resources& ucx);

    const ucx::RegistrationCache& registration_cache() const;

  private:
    ucx::Resources& m_ucx;
};

}  // namespace srf::internal::network
