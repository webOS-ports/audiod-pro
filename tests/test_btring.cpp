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
// "Alerts on phone speaker" keeps the alerts class (palerts, pnotifications,
// pcalendar) on the loudspeaker as well, as legacy webOS did.

namespace {

const std::string BT = "bluez_sink.02_00_00_00_00_01.a2dp_sink";

TEST(BtRing, TheRingtonesStayOnTheLoudspeakerOutsideACall)
{
    EXPECT_EQ(BtRing::classes(eCallMode_None), (std::vector<int>{eringtones}));
    EXPECT_STREQ(BtRing::SPEAKER, "pcm_output");
}

TEST(BtRing, NothingMovedInACarrierCall)
{
    // A ring in a call is call waiting: it goes with the call.
    EXPECT_TRUE(BtRing::classes(eCallMode_Carrier).empty());
}

// D2-7, 2026-10-31: a VoIP call reaches a headset over SCO through the HAL
// (pcm_output), never over A2DP, so its classes stay on pcm_output; the
// ringtones go with the call (call waiting).
TEST(BtRing, AVoipCallKeepsItsOwnClassesOnPcmOutput)
{
    EXPECT_EQ(BtRing::classes(eCallMode_Voip), (std::vector<int>{evoipcall, evoipcall1}));
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
    EXPECT_FALSE(r.classOverride(eCallMode_Voip).active());
    r.outputIs(BT);
    EXPECT_FALSE(r.classOverride(eCallMode_Carrier).active());
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

TEST(BtRing, InACarrierCallTheWholeRangeGoesToTheHeadset)
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


// Where each class of the whole range goes, from the router's runs.
std::vector<std::string> routed(const std::vector<ClassRoute::Range> &runs)
{
    std::vector<std::string> to(eVirtualSink_Last + 1);
    int next = eVirtualSink_First;
    for (const auto &run : runs)
    {
        EXPECT_EQ(run.start, next);
        EXPECT_LE(run.start, run.end);
        for (int c = run.start; c <= run.end && c <= eVirtualSink_Last; c++)
            to[c] = run.sink;
        next = run.end + 1;
    }
    EXPECT_EQ(next, eVirtualSink_Last + 1);
    return to;
}

TEST(BtRingAlerts, OffByDefault)
{
    BtRing r;
    EXPECT_FALSE(r.alertsOnSpeaker());
    r.outputIs(BT);
    EXPECT_EQ(r.classOverride(eCallMode_None).classes, (std::vector<int>{eringtones}));
}

TEST(BtRingAlerts, OnTheAlertsStayOnTheLoudspeakerOutsideACall)
{
    EXPECT_EQ(BtRing::classes(eCallMode_None, true), (std::vector<int>{ealerts, eringtones}));
    EXPECT_EQ(BtRing::classes(eCallMode_None, false), (std::vector<int>{eringtones}));
    EXPECT_TRUE(BtRing::classes(eCallMode_Carrier, true).empty());
    // a VoIP call holds its own classes whatever the setting
    EXPECT_EQ(BtRing::classes(eCallMode_Voip, true), (std::vector<int>{evoipcall, evoipcall1}));
}

TEST(BtRingAlerts, OnTheOverrideHoldsForTheHeadsetOutsideACall)
{
    BtRing r;
    r.setAlertsOnSpeaker(true);
    EXPECT_TRUE(r.alertsOnSpeaker());
    r.outputIs(BT);
    const ClassRoute::Override o = r.classOverride(eCallMode_None);
    EXPECT_TRUE(o.active());
    EXPECT_EQ(o.whileOutput, BT);
    EXPECT_EQ(o.target, "pcm_output");
    EXPECT_EQ(o.classes, (std::vector<int>{ealerts, eringtones}));
}

TEST(BtRingAlerts, OnNothingMovesInACallOrWithoutTheHeadset)
{
    BtRing r;
    r.setAlertsOnSpeaker(true);
    EXPECT_EQ(r.classOverride(eCallMode_None), ClassRoute::Override{});
    r.outputIs(BT);
    EXPECT_EQ(r.classOverride(eCallMode_Carrier), ClassRoute::Override{});
    // a VoIP call moves only its own classes, never the alerts
    EXPECT_EQ(r.classOverride(eCallMode_Voip).classes, (std::vector<int>{evoipcall, evoipcall1}));
    for (const char *other : {"pcm_output", "usb_speaker0", "ring_speaker_usb"})
    {
        r.outputIs(other);
        EXPECT_EQ(r.classOverride(eCallMode_None), ClassRoute::Override{}) << other;
    }
}

TEST(BtRingAlerts, OnTheAlertsAndRingtonesGoToTheLoudspeakerTheRestToTheHeadset)
{
    BtRing r;
    r.setAlertsOnSpeaker(true);
    r.outputIs(BT);
    const auto runs = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, BT,
                                        r.classOverride(eCallMode_None));
    const auto to = routed(runs);
    EXPECT_EQ(to[ealerts], "pcm_output");
    EXPECT_EQ(to[eringtones], "pcm_output");
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
    {
        if (c != ealerts && c != eringtones)
        {
            EXPECT_EQ(to[c], BT) << c;
        }
    }
    // alerts | feedback (keys, DTMF: in the headset) | ringtones | the rest
    ASSERT_EQ(runs.size(), 4u);
    EXPECT_EQ(runs[0], (ClassRoute::Range{ealerts, ealerts, "pcm_output"}));
    EXPECT_EQ(runs[1], (ClassRoute::Range{efeedback, efeedback, BT}));
    EXPECT_EQ(runs[2], (ClassRoute::Range{eringtones, eringtones, "pcm_output"}));
}

TEST(BtRingAlerts, TurnedOffTheAlertsGoBackToTheHeadset)
{
    BtRing r;
    r.outputIs(BT);
    r.setAlertsOnSpeaker(true);
    const ClassRoute::Override on = r.classOverride(eCallMode_None);
    r.setAlertsOnSpeaker(false);
    const ClassRoute::Override off = r.classOverride(eCallMode_None);
    // a different override, so the router routes again at once
    EXPECT_FALSE(on == off);
    EXPECT_EQ(off.classes, (std::vector<int>{eringtones}));
    const auto to = routed(ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, BT, off));
    EXPECT_EQ(to[ealerts], BT);
    EXPECT_EQ(to[eringtones], "pcm_output");
}

TEST(BtRing, InAVoipCallTheCallsClassesGoToPcmOutputTheRestToTheHeadset)
{
    BtRing r;
    r.outputIs(BT);
    const ClassRoute::Override o = r.classOverride(eCallMode_Voip);
    EXPECT_TRUE(o.active());
    EXPECT_EQ(o.whileOutput, BT);
    EXPECT_EQ(o.target, "pcm_output");
    std::vector<std::string> to(eVirtualSink_Last + 1);
    for (const auto &run : ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, BT, o))
        for (int c = run.start; c <= run.end; c++)
            to[c] = run.sink;
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
    {
        const bool call = (c == evoipcall || c == evoipcall1);
        EXPECT_EQ(to[c], call ? "pcm_output" : BT) << c;
    }
}

// With the alerts on the loudspeaker, a VoIP call still moves only its
// own classes: the alerts and the ringtones stay in the headset.
TEST(BtRingAlerts, InAVoipCallOnlyTheCallsClassesLeaveTheHeadset)
{
    BtRing r;
    r.setAlertsOnSpeaker(true);
    r.outputIs(BT);
    const auto to = routed(ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, BT,
                                             r.classOverride(eCallMode_Voip)));
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
    {
        const bool call = (c == evoipcall || c == evoipcall1);
        EXPECT_EQ(to[c], call ? "pcm_output" : BT) << c;
    }
}

// The headset coming back mid-call (D2-7: HFP at 07:40:08, A2DP at
// 07:40:09) finds the override already holding for its sink.
TEST(BtRing, InAVoipCallTheOverrideOutlivesTheHeadset)
{
    BtRing r;
    r.outputIs(BT);
    r.outputIs("pcm_output");
    EXPECT_EQ(r.output(), "");
    EXPECT_EQ(r.lastOutput(), BT);
    const ClassRoute::Override o = r.classOverride(eCallMode_Voip);
    EXPECT_TRUE(o.active());
    EXPECT_EQ(o.whileOutput, BT);
    // outside a call, the ringtones follow only a headset that is there
    EXPECT_FALSE(r.classOverride(eCallMode_None).active());
    // and routed to pcm_output meanwhile, nothing is split
    const auto runs = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, "pcm_output", o);
    ASSERT_EQ(runs.size(), 1u);
}

TEST(BtRing, TheLastHeadsetIsTheNewestBluetoothOutput)
{
    BtRing r;
    EXPECT_EQ(r.lastOutput(), "");
    r.outputIs("usb_speaker0");
    EXPECT_EQ(r.lastOutput(), "");
    r.outputIs(BT);
    r.outputIs("bluez_sink.00_11_22_33_44_55.a2dp_sink");
    r.outputIs("");
    EXPECT_EQ(r.lastOutput(), "bluez_sink.00_11_22_33_44_55.a2dp_sink");
    EXPECT_EQ(r.classOverride(eCallMode_Voip).whileOutput, "bluez_sink.00_11_22_33_44_55.a2dp_sink");
}

}

namespace {

// Every class of the whole range, where the router sends it.
std::vector<std::string> everyClassTo(const BtRing &r, ECallMode mode, const std::string &output)
{
    std::vector<std::string> to(eVirtualSink_Last + 1);
    for (const auto &run : ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, output,
                                             r.classOverride(mode)))
        for (int c = run.start; c <= run.end && c <= eVirtualSink_Last; c++)
            to[c] = run.sink;
    return to;
}

void expectAllOn(const std::vector<std::string> &to, const std::string &sink)
{
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
        EXPECT_EQ(to[c], sink) << c;
}

}

// On a headset's SCO link, a call takes every class off its A2DP sink:
// the headset's A2DP stream starting and stopping broke the call's own
// stream (2026-10-31 09:31:12, 09:31:28).
TEST(BtRingOnHeadsetLink, AVoipCallSendsNothingToA2dp)
{
    BtRing r;
    r.outputIs(BT);
    r.setOnHeadsetLink(true);
    EXPECT_TRUE(r.onHeadsetLink());
    expectAllOn(everyClassTo(r, eCallMode_Voip, BT), "pcm_output");
}

TEST(BtRingOnHeadsetLink, ACarrierCallSendsNothingToA2dp)
{
    BtRing r;
    r.outputIs(BT);
    r.setOnHeadsetLink(true);
    expectAllOn(everyClassTo(r, eCallMode_Carrier, BT), "pcm_output");
    // off the link a carrier call still moves nothing
    r.setOnHeadsetLink(false);
    expectAllOn(everyClassTo(r, eCallMode_Carrier, BT), BT);
}

TEST(BtRingOnHeadsetLink, TheWholeRangeAndNothingElse)
{
    std::vector<int> all;
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
        all.push_back(c);
    EXPECT_EQ(BtRing::classes(eCallMode_Voip, false, true), all);
    EXPECT_EQ(BtRing::classes(eCallMode_Carrier, true, true), all);
    // outside a call the link changes nothing
    EXPECT_EQ(BtRing::classes(eCallMode_None, false, true), (std::vector<int>{eringtones}));
    EXPECT_EQ(BtRing::classes(eCallMode_None, true, true), (std::vector<int>{ealerts, eringtones}));
}

// The Nuraphone on 2026-10-31 09:32:45: its SCO link came up in a WhatsApp
// call, then its A2DP sink came back. Nothing goes to that sink while the
// link is up; once it is down only the call's own classes stay off it,
// and after the call only the ringtones.
TEST(BtRingOnHeadsetLink, AHeadsetReconnectingMidCallGetsNothingUntilTheLinkIsDown)
{
    BtRing r;
    r.outputIs(BT);
    r.outputIs("pcm_output");               // the headset went (09:32:15)
    r.setOnHeadsetLink(true);               // its SCO link is up (09:32:45.85)
    expectAllOn(everyClassTo(r, eCallMode_Voip, BT), "pcm_output");
    r.outputIs(BT);                         // its A2DP sink is back (09:32:46.70)
    expectAllOn(everyClassTo(r, eCallMode_Voip, BT), "pcm_output");
    r.setOnHeadsetLink(false);              // the link closed, the call goes on
    const auto to = everyClassTo(r, eCallMode_Voip, BT);
    for (int c = eVirtualSink_First; c <= eVirtualSink_Last; c++)
    {
        const bool call = (c == evoipcall || c == evoipcall1);
        EXPECT_EQ(to[c], call ? "pcm_output" : BT) << c;
    }
    EXPECT_EQ(r.classOverride(eCallMode_None).classes, (std::vector<int>{eringtones}));
}

// A carrier call on the link holds for the headset that carried the audio
// last as well, gone or not.
TEST(BtRingOnHeadsetLink, ACarrierCallHoldsForTheLastHeadset)
{
    BtRing r;
    r.outputIs(BT);
    r.outputIs("");
    r.setOnHeadsetLink(true);
    const ClassRoute::Override o = r.classOverride(eCallMode_Carrier);
    EXPECT_TRUE(o.active());
    EXPECT_EQ(o.whileOutput, BT);
}
