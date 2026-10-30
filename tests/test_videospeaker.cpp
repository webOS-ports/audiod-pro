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

#include "videoSpeaker.h"

namespace {

// Test plan E04. The manager's /phone route, kept the way the manager keeps
// it: an earpiece or a loudspeaker the call is on, moved as VideoSpeaker says.
struct Call
{
    VideoSpeaker policy;
    bool video = false;
    bool speaker = false;
    bool headset = false;
    bool noSpeaker = false;

    VideoSpeaker::Move step()
    {
        const VideoSpeaker::Move move =
            policy.update({video, !speaker, speaker, headset, noSpeaker});
        if (move == VideoSpeaker::eMove_Speaker)
            speaker = true;
        else if (move == VideoSpeaker::eMove_Earpiece)
            speaker = false;
        return move;
    }

    // The speaker button: what the phone app's setCallMode sends.
    void userSpeaker(bool on)
    {
        policy.userPicked();
        speaker = on;
        step();
    }
};

TEST(VideoSpeaker, AVoiceCallStaysOnTheEarpiece)
{
    Call call;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_FALSE(call.speaker);
    EXPECT_FALSE(call.policy.automatic());
}

TEST(VideoSpeaker, AVideoCallStartsOnTheLoudspeaker)
{
    Call call;
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
    EXPECT_TRUE(call.speaker);
    EXPECT_TRUE(call.policy.automatic());

    // Asked again with nothing changed, it stays put.
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_TRUE(call.speaker);
}

// With a wired or Bluetooth headset in, the call is already somewhere
// private and audible: the earpiece route sends it to the headset.
TEST(VideoSpeaker, AVideoCallWithAHeadsetStaysInTheHeadset)
{
    Call call;
    call.headset = true;
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_FALSE(call.speaker);
}

TEST(VideoSpeaker, TurningOnTheVideoMovesTheCallToTheLoudspeaker)
{
    Call call;
    call.step();
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
    EXPECT_TRUE(call.speaker);
}

TEST(VideoSpeaker, TurningOffTheVideoMovesItBackToTheEarpiece)
{
    Call call;
    call.video = true;
    call.step();
    call.video = false;
    EXPECT_EQ(VideoSpeaker::eMove_Earpiece, call.step());
    EXPECT_FALSE(call.speaker);
    EXPECT_FALSE(call.policy.automatic());
}

// The user's speaker toggle always wins: a loudspeaker they picked is not
// undone when the video stops.
TEST(VideoSpeaker, ALoudspeakerTheUserPickedStaysAfterTheVideo)
{
    Call call;
    call.userSpeaker(true);
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    call.video = false;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_TRUE(call.speaker);
}

// ... nor one they confirmed during the video call.
TEST(VideoSpeaker, TheUserTakingOverTheLoudspeakerKeepsItAfterTheVideo)
{
    Call call;
    call.video = true;
    call.step();
    call.userSpeaker(true);
    EXPECT_FALSE(call.policy.automatic());
    call.video = false;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_TRUE(call.speaker);
}

TEST(VideoSpeaker, TheEarpieceTheUserPicksInAVideoCallStays)
{
    Call call;
    call.video = true;
    call.step();
    call.userSpeaker(false);
    EXPECT_FALSE(call.speaker);

    // Not even a headset coming and going puts it back on the loudspeaker.
    call.headset = true;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    call.headset = false;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_FALSE(call.speaker);
}

// Android switches to the loudspeaker on every upgrade to video, whatever
// was picked while it was a voice call.
TEST(VideoSpeaker, EveryUpgradeToVideoIsANewStart)
{
    Call call;
    call.userSpeaker(false);
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());

    call.userSpeaker(false);
    call.video = false;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
    EXPECT_TRUE(call.speaker);
}

TEST(VideoSpeaker, AHeadsetArrivingTakesTheCallOffTheLoudspeaker)
{
    Call call;
    call.video = true;
    call.step();
    call.headset = true;
    EXPECT_EQ(VideoSpeaker::eMove_Earpiece, call.step());
    EXPECT_FALSE(call.speaker);

    // ... and leaving puts it back, as Android's baseline route for a video
    // call skips the earpiece.
    call.headset = false;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
    EXPECT_TRUE(call.speaker);
}

TEST(VideoSpeaker, AHeadsetLeavingAVideoCallPutsItOnTheLoudspeaker)
{
    Call call;
    call.headset = true;
    call.video = true;
    call.step();
    call.headset = false;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
}

// HAC takes the speakerphone out of service; a video call does not bring it
// back.
TEST(VideoSpeaker, NoLoudspeakerInHearingAidMode)
{
    Call call;
    call.noSpeaker = true;
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_FALSE(call.speaker);
}

// HAC switched on mid-call moves the call to the earpiece itself; nothing
// is left for the video to undo afterwards.
TEST(VideoSpeaker, SomethingElseMovingTheCallEndsTheAutomaticLoudspeaker)
{
    Call call;
    call.video = true;
    call.step();
    call.noSpeaker = true;
    call.speaker = false;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
    EXPECT_FALSE(call.policy.automatic());
    call.video = false;
    EXPECT_EQ(VideoSpeaker::eMove_None, call.step());
}

// A carrier call taking over from a video call is a voice call: the manager
// reports no video for it, so it goes to the ear, and the video call that
// comes back afterwards goes on the loudspeaker again.
TEST(VideoSpeaker, ACarrierCallInBetweenGoesToTheEar)
{
    Call call;
    call.video = true;
    call.step();
    call.video = false;
    EXPECT_EQ(VideoSpeaker::eMove_Earpiece, call.step());
    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
}

TEST(VideoSpeaker, TheNextCallStartsAfresh)
{
    Call call;
    call.video = true;
    call.step();
    call.userSpeaker(false);

    // The manager puts the route back to the earpiece when the last call
    // ends, then tells the policy.
    call.speaker = false;
    call.video = false;
    call.policy.callEnded();
    EXPECT_FALSE(call.policy.automatic());

    call.video = true;
    EXPECT_EQ(VideoSpeaker::eMove_Speaker, call.step());
}

// A route that is neither the earpiece nor the loudspeaker (a headset
// scenario a client picked) is not taken over.
TEST(VideoSpeaker, OnlyTheEarpieceIsReplaced)
{
    VideoSpeaker policy;
    EXPECT_EQ(VideoSpeaker::eMove_None, policy.update({true, false, false, false, false}));
    EXPECT_FALSE(policy.automatic());
}

} // namespace
