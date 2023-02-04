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

#include "mrc/channel/status.hpp"
#include "mrc/channel/v2/concepts.hpp"
#include "mrc/ops/connectable.hpp"
#include "mrc/ops/scheduling_term.hpp"

namespace mrc::ops {

template <channel::concepts::readable_channel ReadableChannelT>
class ChannelReader : public SchedulingTerm<typename ReadableChannelT::value_type, channel::Status>,
                      public Connectable<ReadableChannelT>
{
  public:
    using input_type = Connectable<ReadableChannelT>;

    using Connectable<ReadableChannelT>::Connectable;

    auto operator co_await() -> decltype(auto)
    {
        return Connectable<ReadableChannelT>::channel().async_read();
    }
};

// todo(ryan) - rename to SingleInput
template <typename T>
class AnyChannelReader
{
  public:
    using value_type  = T;
    using error_type  = channel::Status;
    using return_type = expected<value_type, error_type>;
    using task_type   = coroutines::Task<return_type>;
    using input_type  = AnyChannelReader<T>;

    bool is_connected() const
    {
        return bool(m_task_generator != nullptr);
    }

    template <channel::concepts::concrete_readable_channel U>
    requires std::same_as<typename U::value_type, T>
    void connect(std::shared_ptr<U> channel)
    {
        m_task_generator = [channel]() -> task_type {
            co_return co_await channel->async_read();
        };
    }

    template <channel::concepts::type_erased_readable_channel U>
    requires std::same_as<typename U::value_type, T>
    void connect(std::shared_ptr<U> channel)
    {
        m_task_generator = [channel]() -> task_type {
            channel->async_read();
        };
    }

    void disconnect()
    {
        m_task_generator = nullptr;
    }

    [[nodiscard]] auto operator co_await() const noexcept -> decltype(auto)
    {
        return m_task_generator().operator co_await();
    }

  protected:
  private:
    std::function<task_type()> m_task_generator;
};

}  // namespace mrc::ops
