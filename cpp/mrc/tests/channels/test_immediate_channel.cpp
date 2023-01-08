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

#include "mrc/channel/v2/channel.hpp"
#include "mrc/channel/v2/concepts/channel.hpp"
#include "mrc/channel/v2/connectors/channel_provider.hpp"
#include "mrc/channel/v2/cpo/write.hpp"
#include "mrc/channel/v2/immediate_channel.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/generator.hpp"
#include "mrc/coroutines/latch.hpp"
#include "mrc/coroutines/sync_wait.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/coroutines/when_all.hpp"
#include "mrc/ops/handoff.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <coroutine>
#include <utility>

using namespace mrc;
using namespace mrc::channel;
using namespace mrc::channel::v2;

class TestChannelV2 : public ::testing::Test
{
  protected:
    void SetUp() override {}

    void TearDown() override {}

    ImmediateChannel<int> m_channel;

    coroutines::Task<void> int_writer(int iterations, coroutines::Latch& latch)
    {
        for (int i = 0; i < iterations; i++)
        {
            DVLOG(10) << "writing " << i;
            co_await m_channel.async_write(std::move(i));
        }
        latch.count_down();
        DVLOG(10) << "writer done";
        co_return;
    }

    coroutines::Task<> close_on_latch(coroutines::Latch& latch)
    {
        co_await latch;
        DVLOG(10) << "latch completed";
        m_channel.close();
        co_return;
    }

    coroutines::Task<void> int_reader(int iterations)
    {
        int i = 0;
        while (true)
        {
            auto data = co_await m_channel.async_read();
            if (!data)
            {
                break;
            }
            i++;
        }
        EXPECT_EQ(i, iterations);
        co_return;
    }
};

TEST_F(TestChannelV2, ChannelClosed)
{
    ImmediateChannel<int> channel;
    channel.close();

    auto test = [&]() -> coroutines::Task<void> {
        // write should throw
        EXPECT_ANY_THROW(co_await channel.async_write(42));

        // read should return unexpected
        auto data = co_await channel.async_read();
        EXPECT_FALSE(data);

        // task throws
        co_await channel.async_write(42);
        co_return;
    };

    EXPECT_ANY_THROW(coroutines::sync_wait(test()));
}

TEST_F(TestChannelV2, SingleWriterSingleReader)
{
    coroutines::Latch latch{1};
    coroutines::sync_wait(coroutines::when_all(close_on_latch(latch), int_writer(3, latch), int_reader(3)));
}

TEST_F(TestChannelV2, Readerx1_Writer_x1)
{
    coroutines::Latch latch{1};
    coroutines::sync_wait(coroutines::when_all(int_reader(3), int_writer(3, latch), close_on_latch(latch)));
}

TEST_F(TestChannelV2, Readerx2_Writer_x1)
{
    coroutines::Latch latch{1};
    coroutines::sync_wait(
        coroutines::when_all(int_reader(0), int_reader(3), int_writer(3, latch), close_on_latch(latch)));
}

TEST_F(TestChannelV2, Readerx3_Writer_x1)
{
    coroutines::Latch latch{1};
    coroutines::sync_wait(
        coroutines::when_all(close_on_latch(latch), int_reader(0), int_reader(0), int_reader(3), int_writer(3, latch)));
}

TEST_F(TestChannelV2, Readerx4_Writer_x1)
{
    // reader are a lifo, so the first reader in the task list will not get a data entry
    coroutines::Latch latch{1};
    coroutines::sync_wait(coroutines::when_all(
        close_on_latch(latch), int_reader(0), int_reader(0), int_reader(0), int_reader(3), int_writer(3, latch)));
}

TEST_F(TestChannelV2, Readerx3_Writer_x1_Reader_x1)
{
    coroutines::Latch latch{1};
    coroutines::sync_wait(coroutines::when_all(
        int_reader(0), int_reader(0), close_on_latch(latch), int_reader(3), int_writer(3, latch), int_reader(0)));
}

TEST_F(TestChannelV2, Writer_2_Reader_x2)
{
    coroutines::Latch latch{2};
    coroutines::sync_wait(coroutines::when_all(
        int_writer(2, latch), int_writer(2, latch), close_on_latch(latch), int_reader(4), int_reader(0)));
}

class MyChannel
{
  public:
    using data_type = int;

  private:
    struct WriteOperation : public std::suspend_never
    {
        WriteOperation(MyChannel& channel) : m_channel(channel) {}

        MyChannel& m_channel;
    };

    friend auto tag_invoke(unifex::tag_t<channel::v2::cpo::async_write> _, MyChannel& t, int&& data) noexcept
        -> WriteOperation
    {
        return {t};
    }
};

TEST_F(TestChannelV2, WriteCPO)
{
    MyChannel channel;

    auto task = [&]() -> coroutines::Task<> {
        co_await cpo::async_write(channel, 42);
        co_return;
    };

    coroutines::sync_wait(task());
}

TEST_F(TestChannelV2, GenericWriteCPO)
{
    MyChannel channel;

    auto task = [&]() -> coroutines::Task<> {
        co_await cpo::write_task(channel, 42);
        co_return;
    };

    coroutines::sync_wait(task());
}

TEST_F(TestChannelV2, Channel)
{
    auto channel = std::make_unique<ImmediateChannel<int>>();

    auto task = [&]() -> coroutines::Task<> {
        co_await channel->read_task();
        co_await cpo::async_write(*channel, 42);
        co_return;
    };
}

struct Base
{
    virtual ~Base() = default;
};

template <std::movable T>
struct Interface : public Base
{
    virtual void apply(T&& data) = 0;
};

class Concrete final : public Interface<int>
{
  public:
    Concrete(std::function<void()> on_destroy) : m_on_destroy(std::move(on_destroy)) {}
    ~Concrete() final
    {
        if (m_on_destroy)
        {
            m_on_destroy();
        }
    }

    void apply(int&& i) final {}

  private:
    std::function<void()> m_on_destroy;
};

TEST_F(TestChannelV2, VirtualDestructor)
{
    bool triggered = false;

    std::unique_ptr<Interface<int>> i = std::make_unique<Concrete>([&] { triggered = true; });

    i.reset();

    EXPECT_TRUE(triggered);
}

TEST_F(TestChannelV2, ConcereteChannelProvider)
{
    auto concrete = std::make_unique<ImmediateChannel<int>>();
    auto provider = make_channel_provider(std::move(concrete));

    auto readable = provider.readable_channel();
    auto writable = provider.writable_channel();

    static_assert(channel::v2::concepts::concrete_writable<std::decay_t<decltype(*writable)>>);
    static_assert(channel::v2::concepts::concrete_readable<std::decay_t<decltype(*readable)>>);
}

TEST_F(TestChannelV2, GenericChannelProvider)
{
    std::unique_ptr<channel::v2::IChannel<int>> generic = std::make_unique<ImmediateChannel<int>>();

    auto provider = make_channel_provider(std::move(generic));

    auto readable = provider.readable_channel();
    auto writable = provider.writable_channel();

    static_assert(channel::v2::concepts::writable<std::decay_t<decltype(*writable)>>);
    static_assert(channel::v2::concepts::readable<std::decay_t<decltype(*readable)>>);
}

struct IncorrectReadOperation
{
    using data_type = int;

    struct ReadOperation : public std::suspend_never
    {};

    friend auto tag_invoke(unifex::tag_t<cpo::async_read> _, IncorrectReadOperation& t) noexcept -> ReadOperation
    {
        return {};
    }
};

static_assert(!channel::v2::concepts::concrete_readable<IncorrectReadOperation>);

TEST_F(TestChannelV2, Generator)
{
    auto generator = []() -> coroutines::Generator<int> {
        int i = 0;
        while (true)
        {
            co_yield i;
        }
    };

    auto gen = generator();
    auto it  = gen.begin();

    EXPECT_EQ(*it, 0);
    *it = 4;
    it++;
    EXPECT_EQ(*it, 4);
}

TEST_F(TestChannelV2, Handoff)
{
    mrc::ops::Handoff<std::size_t> handoff;

    auto src = [&]() -> coroutines::Task<void> {
        for (std::size_t i = 0; i < 10; i++)
        {
            co_await handoff.write(42);
        }
        handoff.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto data = co_await handoff.read()) {}
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink(), src()));
}
