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

#include "btRing.h"
#include "classRoute.h"

// D1: with a Bluetooth headset carrying the audio, the ringtone (and the
// alarm, which module-palm-policy plays in the ringtones class) rings on the
// loudspeaker; the headset rings out of band through oFono's RING. Media,
// alerts and the rest stay in the headset; a call keeps its own route.

namespace {

const std::string BT = "bluez_sink.02_00_00_00_00_01.a2dp_sink";

TEST(BtRing, TheRingtonesStayOnTheLoudspeakerOutsideACall)
{
    EXPECT_EQ(BtRing::classes(eCallMode_None), (std::vector<int>{eringtones}));
    EXPECT_STREQ(BtRing::SPEAKER, "pcm_output");
}

TEST(BtRing, NothingMovedInACall)
{
    // A ring in a call is call waiting: it goes with the call.
    EXPECT_TRUE(BtRing::classes(eCallMode_Carrier).empty());
    EXPECT_TRUE(BtRing::classes(eCallMode_Voip).empty());
}

TEST(BtRing, OnlyABluetoothOutputIsFollowed)
{
    BtRing r;
    EXPECT_EQ(r.output(), "");
    r.outputIs(BT);
    EXPECT_EQ(r.output(), BT);
    r.outputIs("bluez_output.02_00_00_00_00_01.1");
    EXPECT_EQ(r.output(), "bluez_output.02_00_00_00_00_01.1");
    for (const char *other : {"pcm_output", "usb_speaker0", "ring_speaker_usb", "", "bluez_source.x"})
    {
        r.outputIs(BT);
        r.outputIs(other);
        EXPECT_EQ(r.output(), "") << other;
    }
}

TEST(BtRing, TheOverrideHoldsForTheHeadsetOutsideACall)
{
    BtRing r;
    r.outputIs(BT);
    const ClassRoute::Override o = r.classOverride(eCallMode_None);
    EXPECT_TRUE(o.active());
    EXPECT_EQ(o.whileOutput, BT);
    EXPECT_EQ(o.target, "pcm_output");
    EXPECT_EQ(o.classes, (std::vector<int>{eringtones}));
}

TEST(BtRing, NoOverrideInACallOrWithoutTheHeadset)
{
    BtRing r;
    EXPECT_FALSE(r.classOverride(eCallMode_None).active());
    r.outputIs(BT);
    EXPECT_FALSE(r.classOverride(eCallMode_Carrier).active());
    EXPECT_FALSE(r.classOverride(eCallMode_Voip).active());
    // and an inactive override is the empty one the router compares with
    EXPECT_EQ(r.classOverride(eCallMode_Carrier), ClassRoute::Override{});
    r.outputIs("pcm_output");
    EXPECT_EQ(r.classOverride(eCallMode_None), ClassRoute::Override{});
}

TEST(BtRing, TheRingtonesGoToTheLoudspeakerTheRestToTheHeadset)
{
    BtRing r;
    r.outputIs(BT);
    const auto runs = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, BT,
                                        r.classOverride(eCallMode_None));
    std::vector<std::string> to(eVirtualSink_Last + 1);
    int next = eVirtualSink_First;
    for (const auto &run : runs)
    {
        EXPECT_EQ(run.start, next);
        ASSERT_LE(run.start, run.end);
        for (int c = run.start; c <= run.end; c++)
            to[c] = run.sink;
        next = run.end + 1;
    }
    EXPECT_EQ(next, eVirtualSink_Last + 1);
    EXPECT_EQ(to[eringtones], "pcm_output");
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
    {
        if (c != eringtones)
        {
            EXPECT_EQ(to[c], BT) << c;
        }
    }
    // alerts..feedback | ringtones | the rest
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0], (ClassRoute::Range{ealerts, efeedback, BT}));
    EXPECT_EQ(runs[1], (ClassRoute::Range{eringtones, eringtones, "pcm_output"}));
}

TEST(BtRing, InACallTheWholeRangeGoesToTheHeadset)
{
    BtRing r;
    r.outputIs(BT);
    const auto runs = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, BT,
                                        r.classOverride(eCallMode_Carrier));
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0], (ClassRoute::Range{eVirtualSink_First, eVirtualSink_Last, BT}));
}

TEST(BtRing, ItDoesNothingForTheLoudspeakerItself)
{
    // The override is for the headset: routed to pcm_output (the headset
    // gone, but its name still kept), the range is one run.
    BtRing r;
    r.outputIs(BT);
    const auto runs = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, "pcm_output",
                                        r.classOverride(eCallMode_None));
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].sink, "pcm_output");
}

}
