// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "shutdownGuard.h"

#include <gtest/gtest.h>

#include <csignal>
#include <future>

using namespace std::chrono;
using Clock = steady_clock;

namespace
{
    const milliseconds kDeadline(200);
    //! Long enough for a thread to have run, short enough for the suite.
    const milliseconds kSettle(600);

    //! Counts the calls and lets a test wait for the first.
    struct Calls
    {
        std::atomic<int> count{0};
        std::promise<Clock::time_point> first;

        ShutdownGuard::Action action()
        {
            return [this] {
                if (count.fetch_add(1) == 0)
                    first.set_value(Clock::now());
            };
        }
    };
}

TEST(ShutdownGuard, NothingHappensWithoutARequest)
{
    Calls quit, expire;
    {
        ShutdownGuard guard(quit.action(), expire.action(), kDeadline);
        std::this_thread::sleep_for(kDeadline * 2);
    }
    EXPECT_EQ(0, quit.count);
    EXPECT_EQ(0, expire.count);
}

TEST(ShutdownGuard, ARequestQuitsTheLoop)
{
    Calls quit, expire;
    ShutdownGuard guard(quit.action(), expire.action(), kDeadline);
    auto quitAt = quit.first.get_future();

    guard.request();
    ASSERT_EQ(std::future_status::ready, quitAt.wait_for(kSettle));
    guard.finished();
    std::this_thread::sleep_for(kDeadline * 2);
    EXPECT_EQ(1, quit.count);
    EXPECT_EQ(0, expire.count);
}

TEST(ShutdownGuard, AShutdownThatOverrunsExpiresAtTheDeadline)
{
    Calls quit, expire;
    ShutdownGuard guard(quit.action(), expire.action(), kDeadline);
    auto expiredAt = expire.first.get_future();

    const auto requestedAt = Clock::now();
    guard.request();
    ASSERT_EQ(std::future_status::ready, expiredAt.wait_for(kDeadline + kSettle));
    const auto took = duration_cast<milliseconds>(expiredAt.get() - requestedAt);
    EXPECT_GE(took, kDeadline);
    EXPECT_LT(took, kDeadline + kSettle);
    EXPECT_EQ(1, quit.count);
    EXPECT_EQ(1, expire.count);
}

TEST(ShutdownGuard, AQuitThatBlocksDoesNotHoldTheDeadlineBack)
{
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    Calls expire;
    {
        ShutdownGuard guard([released] { released.wait(); }, expire.action(), kDeadline);
        auto expiredAt = expire.first.get_future();

        guard.request();
        EXPECT_EQ(std::future_status::ready, expiredAt.wait_for(kDeadline + kSettle));
        release.set_value();
    }
    EXPECT_EQ(1, expire.count);
}

TEST(ShutdownGuard, FinishingInTimeLeavesItRunning)
{
    Calls quit, expire;
    ShutdownGuard guard(quit.action(), expire.action(), kDeadline);

    guard.request();
    std::this_thread::sleep_for(kDeadline / 4);
    guard.finished();
    std::this_thread::sleep_for(kDeadline * 2);
    EXPECT_EQ(0, expire.count);
}

TEST(ShutdownGuard, SigtermRequestsTheShutdown)
{
    Calls quit, expire;
    ShutdownGuard guard(quit.action(), expire.action(), kDeadline);
    ASSERT_TRUE(guard.catchSignals());
    auto quitAt = quit.first.get_future();

    ASSERT_EQ(0, raise(SIGTERM));
    ASSERT_EQ(std::future_status::ready, quitAt.wait_for(kSettle));
    guard.finished();
    EXPECT_EQ(1, quit.count);
}

TEST(ShutdownGuard, SigintRequestsTheShutdown)
{
    Calls quit, expire;
    ShutdownGuard guard(quit.action(), expire.action(), kDeadline);
    ASSERT_TRUE(guard.catchSignals());
    auto quitAt = quit.first.get_future();

    ASSERT_EQ(0, raise(SIGINT));
    ASSERT_EQ(std::future_status::ready, quitAt.wait_for(kSettle));
    guard.finished();
}

TEST(ShutdownGuard, OneGuardCatchesTheSignalsAtATime)
{
    Calls quit, expire;
    {
        ShutdownGuard first(quit.action(), expire.action(), kDeadline);
        ASSERT_TRUE(first.catchSignals());
        ShutdownGuard second(quit.action(), expire.action(), kDeadline);
        EXPECT_FALSE(second.catchSignals());
    }
    ShutdownGuard third(quit.action(), expire.action(), kDeadline);
    EXPECT_TRUE(third.catchSignals());
}

TEST(ShutdownGuard, GoingAwayGivesTheSignalsBack)
{
    Calls quit, expire;
    {
        ShutdownGuard guard(quit.action(), expire.action(), kDeadline);
        ASSERT_TRUE(guard.catchSignals());
    }
    for (int sig : {SIGTERM, SIGINT})
    {
        struct sigaction now = {};
        ASSERT_EQ(0, sigaction(sig, nullptr, &now));
        EXPECT_EQ(SIG_DFL, now.sa_handler) << "signal " << sig;
    }
}
