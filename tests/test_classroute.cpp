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

#include <pulse/module-palm-policy.h>

#include "classRoute.h"

// The router sending a few stream classes elsewhere than the rest of a
// display's while one output is in use.

namespace {

const char *const COMBINED = "ring_speaker_usb";

ClassRoute::Override usbOverride(const std::vector<int> &classes)
{
    return ClassRoute::Override{"usb_speaker0", COMBINED, classes};
}

TEST(ClassRoute, WithoutAnOverrideTheWholeRangeGoesToTheOutput)
{
    const auto r = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, "usb_speaker0",
                                     ClassRoute::Override{});
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], (ClassRoute::Range{eVirtualSink_First, eVirtualSink_Last, "usb_speaker0"}));
}

TEST(ClassRoute, TheOverrideHoldsOnlyForItsOutput)
{
    const auto r = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, "pcm_output",
                                     usbOverride({ealerts, eringtones}));
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].sink, "pcm_output");
}

TEST(ClassRoute, NeighboursGoingTheSameWayAreOneRun)
{
    const auto r = ClassRoute::split(0, 5, "usb_speaker0", usbOverride({2, 3}));
    EXPECT_EQ(r, (std::vector<ClassRoute::Range>{{0, 1, "usb_speaker0"},
                                                 {2, 3, COMBINED},
                                                 {4, 5, "usb_speaker0"}}));
}

TEST(ClassRoute, ClassesOutsideTheRangeAreLeftAlone)
{
    const auto r = ClassRoute::split(4, 6, "usb_speaker0", usbOverride({0, 2}));
    EXPECT_EQ(r, (std::vector<ClassRoute::Range>{{4, 6, "usb_speaker0"}}));
}

TEST(ClassRoute, AnEmptyOverrideIsInactive)
{
    EXPECT_FALSE(usbOverride({}).active());
    EXPECT_FALSE((ClassRoute::Override{"usb_speaker0", "", {1}}).active());
    EXPECT_FALSE((ClassRoute::Override{"", COMBINED, {1}}).active());
    EXPECT_TRUE(usbOverride({1}).active());
}

}
