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

#include <gtest/gtest.h>

#include "paReconnect.h"

namespace {

// PulseAudio restarting takes about a second; the first try comes well
// before that, and the new context waits for the daemon from there.
TEST(PaReconnect, TheFirstLossWaitsShortly)
{
    PaReconnect reconnect;

    EXPECT_EQ(reconnect.lost(), PaReconnect::kFirstDelayMs);
}

// A context that fails again right away waits longer each time, and never
// longer than the cap.
TEST(PaReconnect, LossesInARowBackOffToTheCap)
{
    PaReconnect reconnect;

    EXPECT_EQ(reconnect.lost(), 500u);
    EXPECT_EQ(reconnect.lost(), 1000u);
    EXPECT_EQ(reconnect.lost(), 2000u);
    EXPECT_EQ(reconnect.lost(), 4000u);
    EXPECT_EQ(reconnect.lost(), PaReconnect::kMaxDelayMs);
    EXPECT_EQ(reconnect.lost(), PaReconnect::kMaxDelayMs);
}

// However long PulseAudio stays away, the wait stays at the cap.
TEST(PaReconnect, ALongOutageStaysAtTheCap)
{
    PaReconnect reconnect;

    for (int i = 0; i < 1000; ++i)
        EXPECT_LE(reconnect.lost(), PaReconnect::kMaxDelayMs);
    EXPECT_EQ(reconnect.lost(), PaReconnect::kMaxDelayMs);
}

// Once a connection has come up, the next restart is picked up as quickly as
// the first.
TEST(PaReconnect, AConnectionStartsTheWaitOver)
{
    PaReconnect reconnect;

    reconnect.lost();
    reconnect.lost();
    reconnect.lost();
    reconnect.connected();
    EXPECT_EQ(reconnect.lost(), PaReconnect::kFirstDelayMs);
    EXPECT_EQ(reconnect.lost(), 2 * PaReconnect::kFirstDelayMs);
}

} // namespace
