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

#include "srf/coro/event.hpp"
#include "srf/coro/latch.hpp"
#include "srf/coro/task.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace srf;

class TestCoroLatch : public ::testing::Test
{};

TEST_F(TestCoroLatch, Count0)
{
    coro::Latch l{0};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        co_await l;
        co_return 42;
    };

    auto task = make_task();

    task.resume();

    EXPECT_TRUE(task.is_ready());
    EXPECT_EQ(task.promise().result(), 42);
}

TEST_F(TestCoroLatch, Count1)
{
    coro::Latch l{1};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down();
    EXPECT_TRUE(task.is_ready());
    EXPECT_EQ(task.promise().result(), 1);
}

TEST_F(TestCoroLatch, Count1Down5)
{
    coro::Latch l{1};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down(5);
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 1);
}

// TEST_CASE("latch count=5 count_down=1 x5", "[latch]")
TEST_F(TestCoroLatch, Count5Down1x5)
{
    coro::Latch l{5};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 5);
}

// TEST_CASE("latch count=5 count_down=5", "[latch]")
TEST_F(TestCoroLatch, Count5Down5)
{
    coro::Latch l{5};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down(5);
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 5);
}
