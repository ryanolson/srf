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

#include "mrc/channel/v2/readable_channel.hpp"
#include "mrc/channel/v2/writable_channel.hpp"
#include "mrc/coroutines/task.hpp"

namespace mrc::channel::v2 {

template <typename T>
class GenericChannel : public IReadableChannel<T>, public IWritableChannel<T>
{
  public:
    template <typename ChannelT>
    GenericChannel(std::shared_ptr<ChannelT> channel)
    {
        CHECK(channel);

        m_reader_task = [channel]() -> coroutines::Task<expected<T, Status>> {
            co_return co_await channel->async_read();
        };

        m_writer_task = [channel](T&& data) -> coroutines::Task<> {
            co_await channel->async_write(std::move(data));
            co_return;
        };

        m_close_task = [channel]() { channel->close(); };
    }

    ~GenericChannel() override = default;

    coroutines::Task<expected<T, Status>> async_read() final
    {
        return m_reader_task();
    }

    coroutines::Task<> async_write(T&& data) final
    {
        return m_writer_task(std::move(data));
    }

    void close()
    {
        m_close_task();
    }

  private:
    std::function<coroutines::Task<expected<T, Status>>()> m_reader_task;
    std::function<coroutines::Task<>(T&&)> m_writer_task;
    std::function<void()> m_close_task;
};

}  // namespace mrc::channel::v2
