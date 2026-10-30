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

#include "callState.h"

namespace {

// The senders as the bus names them: the connectors all live in
// imlibpurpleservice, a luna-send has only its unique name.
const std::string kPurple = "com.palm.imlibpurple";
const std::string kDiscord = "com.palm.discord.calling";
const std::string kLunaSend = ":1.42";

const std::string kTelephony = "com.palm.telephony";
const std::string kWhatsApp = "com.palm.whatsapp";
const std::string kTelegram = "com.palm.telegram";
const std::string kTeams = "com.palm.teams";

CallState::Line line(ECallStatus status, const std::string &transport, bool video = false)
{
    return CallState::Line{status, transport, video};
}

TEST(CallState, StatesAreReadAsTheLegacyPhoneAppSpellsThem)
{
    EXPECT_EQ(eCallStatus_Active, CallState::statusFromString("active"));
    EXPECT_EQ(eCallStatus_Incoming, CallState::statusFromString("incoming"));
    EXPECT_EQ(eCallStatus_Connecting, CallState::statusFromString("connecting"));
    EXPECT_EQ(eCallStatus_Dialing, CallState::statusFromString("dialing"));
    EXPECT_EQ(eCallStatus_OnHold, CallState::statusFromString("onHold"));
    EXPECT_EQ(eCallStatus_Disconnected, CallState::statusFromString("disconnected"));
    EXPECT_EQ(eCallStatus_NoCall, CallState::statusFromString("held"));
    EXPECT_EQ(eCallStatus_NoCall, CallState::statusFromString(""));
}

// A call needs its audio path while talking and on hold - not while it only
// rings, or the ringtone would lose the loudspeaker and the music would stop
// before anyone answered.
TEST(CallState, OnlyActiveAndHeldCallsNeedTheCallPath)
{
    EXPECT_TRUE(CallState::inCall(eCallStatus_Active));
    EXPECT_TRUE(CallState::inCall(eCallStatus_OnHold));
    EXPECT_FALSE(CallState::inCall(eCallStatus_Incoming));
    EXPECT_FALSE(CallState::inCall(eCallStatus_Dialing));
    EXPECT_FALSE(CallState::inCall(eCallStatus_Connecting));
    EXPECT_FALSE(CallState::inCall(eCallStatus_Disconnected));
    EXPECT_FALSE(CallState::inCall(eCallStatus_NoCall));

    CallState state;
    state.report(kPurple, {line(eCallStatus_Incoming, kWhatsApp)});
    EXPECT_EQ(eCallMode_None, state.mode());
}

TEST(CallState, NoCallsAtFirst)
{
    CallState state;
    EXPECT_EQ(eCallMode_None, state.mode());
    EXPECT_FALSE(state.video());
}

TEST(CallState, ThePhoneAppsCellularCallIsACarrierCall)
{
    CallState state;
    state.setCarrierCall(true);
    EXPECT_EQ(eCallMode_Carrier, state.mode());
    state.setCarrierCall(false);
    EXPECT_EQ(eCallMode_None, state.mode());
}

// What the connectors send: an active line with their own transport, then
// an empty list when the call ends.
TEST(CallState, AConnectorsCallIsVoip)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp)});
    EXPECT_EQ(eCallMode_Voip, state.mode());
    state.report(kPurple, {});
    EXPECT_EQ(eCallMode_None, state.mode());
}

// Test plan E02: a cellular call during a VoIP call. The connector hanging
// up used to end the cellular call too, and the music came back mid-call.
TEST(CallState, AConnectorHangingUpDoesNotEndTheCellularCall)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp)});
    state.setCarrierCall(true);
    EXPECT_EQ(eCallMode_Carrier, state.mode());

    state.report(kPurple, {});
    EXPECT_EQ(eCallMode_Carrier, state.mode());
}

// ... and the other way round: the VoIP call goes on after the cellular one.
TEST(CallState, TheVoipCallGoesOnWhenTheCellularCallEnds)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kTelegram)});
    state.setCarrierCall(true);
    state.setCarrierCall(false);
    EXPECT_EQ(eCallMode_Voip, state.mode());
}

TEST(CallState, OneSenderHangingUpDoesNotEndAnothersCall)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp)});
    state.report(kDiscord, {line(eCallStatus_Active, "com.palm.discord")});
    state.report(kDiscord, {});
    EXPECT_EQ(eCallMode_Voip, state.mode());
    state.report(kPurple, {});
    EXPECT_EQ(eCallMode_None, state.mode());
}

// Test plan D05: two calls, the first one ends. The last line used to decide
// on its own, so a disconnected second line ended the call that was still up.
TEST(CallState, EveryLineCountsNotTheLastOne)
{
    CallState state;
    state.report(kLunaSend, {line(eCallStatus_Active, kTelephony),
                             line(eCallStatus_Disconnected, kTelephony)});
    EXPECT_EQ(eCallMode_Carrier, state.mode());

    state.report(kLunaSend, {line(eCallStatus_Disconnected, kTelephony),
                             line(eCallStatus_Active, kTelephony)});
    EXPECT_EQ(eCallMode_Carrier, state.mode());

    state.report(kLunaSend, {line(eCallStatus_Active, kWhatsApp),
                             line(eCallStatus_Incoming, kWhatsApp)});
    EXPECT_EQ(eCallMode_Voip, state.mode());
}

// Test plan D03: with the only remaining call on hold, the call path stays.
TEST(CallState, AHeldCallKeepsTheCallPath)
{
    CallState state;
    state.report(kLunaSend, {line(eCallStatus_OnHold, kTelephony)});
    EXPECT_EQ(eCallMode_Carrier, state.mode());
}

TEST(CallState, ACarrierCallWinsOverVoip)
{
    CallState state;
    state.report(kLunaSend, {line(eCallStatus_Active, kWhatsApp),
                             line(eCallStatus_OnHold, kTelephony)});
    EXPECT_EQ(eCallMode_Carrier, state.mode());

    CallState other;
    other.report(kPurple, {line(eCallStatus_Active, kWhatsApp)});
    other.report(kLunaSend, {line(eCallStatus_Active, kTelephony)});
    EXPECT_EQ(eCallMode_Carrier, other.mode());
}

// Teams sends a made-up carrier line before its own one (a webOS 3.0.5
// workaround). An update is the whole truth for its sender, so the made-up
// line does not outlive the real one.
TEST(CallState, AnUpdateReplacesWhatTheSenderReportedBefore)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kTelephony)});
    state.report(kPurple, {line(eCallStatus_Active, kTeams)});
    EXPECT_EQ(eCallMode_Voip, state.mode());

    state.report(kPurple, {line(eCallStatus_Disconnected, kTeams)});
    EXPECT_EQ(eCallMode_None, state.mode());
}

// A one-shot luna-send is a new sender every time: its "no calls" means what
// it meant when the phone app was the only one sending it.
TEST(CallState, NoCallsFromANewSenderEndsEveryReportedCall)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp)});
    state.report(":1.43", {line(eCallStatus_Active, kTelephony)});
    state.report(kLunaSend, {});
    EXPECT_EQ(eCallMode_None, state.mode());
}

// ... but not the phone app's own cellular call, which it reports apart.
TEST(CallState, NoCallsFromANewSenderLeavesTheCellularCall)
{
    CallState state;
    state.setCarrierCall(true);
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp)});
    state.report(kLunaSend, {});
    EXPECT_EQ(eCallMode_Carrier, state.mode());
}

// A sender that reported only a ringing call is known: its "no calls" ends
// only its own.
TEST(CallState, NoCallsFromASenderThatRangEndsOnlyItsOwn)
{
    CallState state;
    state.report(kDiscord, {line(eCallStatus_Active, "com.palm.discord")});
    state.report(kPurple, {line(eCallStatus_Incoming, kWhatsApp)});
    EXPECT_EQ(eCallMode_Voip, state.mode());
    state.report(kPurple, {});
    EXPECT_EQ(eCallMode_Voip, state.mode());
}

TEST(CallState, VideoIsAVoipCallsAndEndsWithIt)
{
    CallState state;
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp, true)});
    EXPECT_TRUE(state.video());
    state.report(kPurple, {});
    EXPECT_FALSE(state.video());

    state.report(kPurple, {line(eCallStatus_Incoming, kWhatsApp, true)});
    EXPECT_FALSE(state.video());

    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp, false),
                           line(eCallStatus_OnHold, kTelegram, true)});
    EXPECT_TRUE(state.video());
    state.report(kPurple, {line(eCallStatus_Active, kWhatsApp, true),
                           line(eCallStatus_OnHold, kTelegram, false)});
    EXPECT_TRUE(state.video());

    // A carrier line carries no video of ours.
    state.report(kPurple, {line(eCallStatus_Active, kTelephony, true)});
    EXPECT_FALSE(state.video());
}

// R1-R5 of the test plan: media gives way to the first call and comes back
// after the last; switching between a carrier and a VoIP call is neither.
TEST(CallState, MediaPausesForTheFirstCallAndResumesAfterTheLast)
{
    auto t = CallState::transition(eCallMode_None, eCallMode_Carrier);
    EXPECT_TRUE(t.pauseMedia);
    EXPECT_FALSE(t.resumeMedia);
    EXPECT_FALSE(t.resetSpeaker);
    EXPECT_FALSE(t.resetMute);

    t = CallState::transition(eCallMode_None, eCallMode_Voip);
    EXPECT_TRUE(t.pauseMedia);
    EXPECT_FALSE(t.resumeMedia);

    t = CallState::transition(eCallMode_Carrier, eCallMode_None);
    EXPECT_FALSE(t.pauseMedia);
    EXPECT_TRUE(t.resumeMedia);
    EXPECT_TRUE(t.resetSpeaker);
    EXPECT_TRUE(t.resetMute);

    t = CallState::transition(eCallMode_Voip, eCallMode_None);
    EXPECT_TRUE(t.resumeMedia);
    EXPECT_TRUE(t.resetSpeaker);
    EXPECT_TRUE(t.resetMute);
}

TEST(CallState, MovingBetweenCallsIsNotATransition)
{
    for (auto from : {eCallMode_Carrier, eCallMode_Voip})
    {
        for (auto to : {eCallMode_Carrier, eCallMode_Voip})
        {
            auto t = CallState::transition(from, to);
            EXPECT_FALSE(t.pauseMedia);
            EXPECT_FALSE(t.resumeMedia);
            EXPECT_FALSE(t.resetSpeaker);
            EXPECT_FALSE(t.resetMute);
        }
    }
    auto t = CallState::transition(eCallMode_None, eCallMode_None);
    EXPECT_FALSE(t.pauseMedia);
    EXPECT_FALSE(t.resumeMedia);
    EXPECT_FALSE(t.resetSpeaker);
    EXPECT_FALSE(t.resetMute);
}

} // namespace

TEST(CallState, UnpluggingPausesTheMusic)
{
    const CallState::Unplug u = CallState::unplugged(eCallMode_None, false);
    EXPECT_TRUE(u.pauseMedia);
    EXPECT_FALSE(u.keepPausedAfterCall);
}

TEST(CallState, UnpluggingInACallKeepsTheMusicPausedAfterIt)
{
    for (auto mode : {eCallMode_Carrier, eCallMode_Voip})
    {
        const CallState::Unplug u = CallState::unplugged(mode, false);
        EXPECT_FALSE(u.pauseMedia);
        EXPECT_TRUE(u.keepPausedAfterCall);
    }
}

TEST(CallState, UnpluggingLeavesBluetoothAudioAlone)
{
    // The music is in the Bluetooth headset, not in the plug that was pulled.
    for (auto mode : {eCallMode_None, eCallMode_Carrier, eCallMode_Voip})
    {
        const CallState::Unplug u = CallState::unplugged(mode, true);
        EXPECT_FALSE(u.pauseMedia);
        EXPECT_FALSE(u.keepPausedAfterCall);
    }
}
