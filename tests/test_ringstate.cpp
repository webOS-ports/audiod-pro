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

#include "ringState.h"

namespace {

// Ringtones and alarms are what module-palm-policy tags "pringtones" and
// "palarm"; music, notifications and calls are not ringing.
TEST(RingState, RingtonesAndAlarmsRing)
{
    EXPECT_TRUE(RingState::isRingType("pringtones"));
    EXPECT_TRUE(RingState::isRingType("palarm"));
    EXPECT_FALSE(RingState::isRingType("pmedia"));
    EXPECT_FALSE(RingState::isRingType("pdefaultapp"));
    EXPECT_FALSE(RingState::isRingType("pnotifications"));
    EXPECT_FALSE(RingState::isRingType("voipcall"));
    EXPECT_FALSE(RingState::isRingType("pringtones2"));
    EXPECT_FALSE(RingState::isRingType(""));
    EXPECT_FALSE(RingState::isRingType(nullptr));
}

TEST(RingState, NothingRingsAtFirst)
{
    RingState state;
    EXPECT_FALSE(state.ringing());
}

TEST(RingState, ARingtoneRingsUntilItGoes)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(7, "pringtones", false));
    EXPECT_TRUE(state.ringing());
    EXPECT_TRUE(state.streamGone(7));
    EXPECT_FALSE(state.ringing());
}

// A paused (corked) ringtone does not ring; uncorked again, it does.
TEST(RingState, ACorkedRingtoneIsSilent)
{
    RingState state;
    EXPECT_FALSE(state.streamSeen(7, "pringtones", true));
    EXPECT_FALSE(state.ringing());
    EXPECT_TRUE(state.streamSeen(7, "pringtones", false));
    EXPECT_TRUE(state.streamSeen(7, "pringtones", true));
    EXPECT_FALSE(state.ringing());
}

TEST(RingState, OtherStreamsChangeNothing)
{
    RingState state;
    EXPECT_FALSE(state.streamSeen(3, "pmedia", false));
    EXPECT_FALSE(state.streamSeen(4, nullptr, false));
    EXPECT_FALSE(state.ringing());
    EXPECT_FALSE(state.streamGone(3));
    EXPECT_FALSE(state.streamGone(99));
}

// An alarm going off while the phone rings: it rings until both are gone,
// and only the first start and the last stop are changes.
TEST(RingState, RingsUntilTheLastOneGoes)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(7, "pringtones", false));
    EXPECT_FALSE(state.streamSeen(8, "palarm", false));
    EXPECT_FALSE(state.streamGone(7));
    EXPECT_TRUE(state.ringing());
    EXPECT_TRUE(state.streamGone(8));
    EXPECT_FALSE(state.ringing());
}

// The same stream reported again (a volume change) is not a new ring.
TEST(RingState, TheSameStreamTwiceIsOneRing)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(7, "pringtones", false));
    EXPECT_FALSE(state.streamSeen(7, "pringtones", false));
    EXPECT_TRUE(state.streamGone(7));
}

TEST(RingState, PulseAudioGoingAwayEndsTheRinging)
{
    RingState state;
    state.streamSeen(7, "pringtones", false);
    state.streamSeen(8, "palarm", false);
    state.clear();
    EXPECT_FALSE(state.ringing());
    // The indices mean nothing any more: a new stream with an old index rings.
    EXPECT_TRUE(state.streamSeen(7, "pringtones", false));
}

// Outside a call a ring goes to the loudspeaker as well; during a call it is
// call waiting, which stays in the ear.
TEST(RingState, TheLoudspeakerOnlyOutsideACall)
{
    EXPECT_TRUE(RingState::speakerToo(eCallMode_None, true));
    EXPECT_FALSE(RingState::speakerToo(eCallMode_None, false));
    EXPECT_FALSE(RingState::speakerToo(eCallMode_Carrier, true));
    EXPECT_FALSE(RingState::speakerToo(eCallMode_Voip, true));
    EXPECT_FALSE(RingState::speakerToo(eCallMode_Carrier, false));
}

} // namespace
