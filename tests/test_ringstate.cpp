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

// Key clicks, the voice of a call and streams with no tag are none of
// ringing, alerts or music.
TEST(RingState, OtherStreamsChangeNothing)
{
    RingState state;
    EXPECT_FALSE(state.streamSeen(3, "pfeedback", false));
    EXPECT_FALSE(state.streamSeen(4, nullptr, false));
    EXPECT_FALSE(state.streamSeen(5, "voipcall", false));
    EXPECT_FALSE(state.ringing());
    EXPECT_FALSE(state.alerting());
    EXPECT_FALSE(state.mediaPlaying());
    EXPECT_FALSE(state.speakerWanted(true));
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

// The alerts are what module-palm-policy tags "palerts", "pnotifications" and
// "pcalendar" (legacy webOS's mask 0x803); the music "pmedia" and
// "pdefaultapp".
TEST(RingState, AlertsAndMusicAreTold)
{
    EXPECT_TRUE(RingState::isAlertType("palerts"));
    EXPECT_TRUE(RingState::isAlertType("pnotifications"));
    EXPECT_TRUE(RingState::isAlertType("pcalendar"));
    EXPECT_FALSE(RingState::isAlertType("pringtones"));
    EXPECT_FALSE(RingState::isAlertType("palarm"));
    EXPECT_FALSE(RingState::isAlertType("pmedia"));
    EXPECT_FALSE(RingState::isAlertType("pfeedback"));
    EXPECT_FALSE(RingState::isAlertType("palerts2"));
    EXPECT_FALSE(RingState::isAlertType(""));
    EXPECT_FALSE(RingState::isAlertType(nullptr));

    EXPECT_TRUE(RingState::isMediaType("pmedia"));
    EXPECT_TRUE(RingState::isMediaType("pdefaultapp"));
    EXPECT_FALSE(RingState::isMediaType("pnotifications"));
    EXPECT_FALSE(RingState::isMediaType("pringtones"));
    EXPECT_FALSE(RingState::isMediaType("ptts"));
    EXPECT_FALSE(RingState::isMediaType(""));
    EXPECT_FALSE(RingState::isMediaType(nullptr));
}

// A ringtone or an alarm goes to the loudspeaker as well, ringer switch or
// not (a silenced ringtone is muted, not moved).
TEST(RingState, ARingWantsTheLoudspeaker)
{
    RingState state;
    EXPECT_FALSE(state.speakerWanted(true));
    state.streamSeen(7, "pringtones", false);
    EXPECT_TRUE(state.speakerWanted(true));
    EXPECT_TRUE(state.speakerWanted(false));
    state.streamGone(7);
    state.streamSeen(8, "palarm", false);
    EXPECT_TRUE(state.speakerWanted(false));
}

// A notification with headphones in: the loudspeaker too with the ringer
// switch on, the headphones alone with it off. It is no ringing.
TEST(RingState, AnAlertWantsTheLoudspeakerWithTheRingerOn)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(5, "pnotifications", false));
    EXPECT_TRUE(state.alerting());
    EXPECT_FALSE(state.ringing());
    EXPECT_TRUE(state.speakerWanted(true));
    EXPECT_FALSE(state.speakerWanted(false));
    EXPECT_TRUE(state.streamGone(5));
    EXPECT_FALSE(state.alerting());
    EXPECT_FALSE(state.speakerWanted(true));
}

TEST(RingState, ACorkedAlertIsSilent)
{
    RingState state;
    EXPECT_FALSE(state.streamSeen(5, "palerts", true));
    EXPECT_FALSE(state.speakerWanted(true));
    EXPECT_TRUE(state.streamSeen(5, "palerts", false));
    EXPECT_TRUE(state.speakerWanted(true));
}

// The jack and the loudspeaker are one port: while music plays an alert
// stays in the headphones, or the music would be heard out loud too. Once
// the music is paused or gone, the next alert has the loudspeaker again.
TEST(RingState, AnAlertStaysInTheHeadphonesWhileMusicPlays)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(3, "pmedia", false));
    EXPECT_TRUE(state.mediaPlaying());
    EXPECT_FALSE(state.speakerWanted(true));
    EXPECT_TRUE(state.streamSeen(5, "pcalendar", false));
    EXPECT_FALSE(state.speakerWanted(true));

    // Paused: that is a change the manager has to act on.
    EXPECT_TRUE(state.streamSeen(3, "pmedia", true));
    EXPECT_FALSE(state.mediaPlaying());
    EXPECT_TRUE(state.speakerWanted(true));

    EXPECT_TRUE(state.streamSeen(4, "pdefaultapp", false));
    EXPECT_FALSE(state.speakerWanted(true));
    EXPECT_TRUE(state.streamGone(4));
    EXPECT_TRUE(state.speakerWanted(true));
}

// A call or an alarm is not missed for the music: those ring on the
// loudspeaker with music playing as well.
TEST(RingState, ARingGoesToTheLoudspeakerOverMusic)
{
    RingState state;
    state.streamSeen(3, "pmedia", false);
    state.streamSeen(7, "pringtones", false);
    EXPECT_TRUE(state.speakerWanted(true));
    EXPECT_TRUE(state.speakerWanted(false));
}

// Music starting or stopping alone is reported, so an alert already playing
// moves; a second music stream is no change.
TEST(RingState, MusicComingAndGoingIsAChange)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(3, "pmedia", false));
    EXPECT_FALSE(state.streamSeen(4, "pmedia", false));
    EXPECT_FALSE(state.streamGone(3));
    EXPECT_TRUE(state.streamGone(4));
}

// A second alert, or one while the phone rings, is no change of its own.
TEST(RingState, AlertsAndRingsTogether)
{
    RingState state;
    EXPECT_TRUE(state.streamSeen(5, "palerts", false));
    EXPECT_FALSE(state.streamSeen(6, "pnotifications", false));
    EXPECT_TRUE(state.streamSeen(7, "pringtones", false));
    EXPECT_FALSE(state.streamGone(5));
    EXPECT_TRUE(state.streamGone(7));
    EXPECT_TRUE(state.speakerWanted(true));
    EXPECT_TRUE(state.streamGone(6));
    EXPECT_FALSE(state.speakerWanted(true));
}

TEST(RingState, PulseAudioGoingAwayEndsAlertsAndMusic)
{
    RingState state;
    state.streamSeen(3, "pmedia", false);
    state.streamSeen(5, "palerts", false);
    state.clear();
    EXPECT_FALSE(state.alerting());
    EXPECT_FALSE(state.mediaPlaying());
    // An old index is a new stream, of whatever kind it says now.
    EXPECT_TRUE(state.streamSeen(3, "palerts", false));
    EXPECT_TRUE(state.speakerWanted(true));
}

// The same index reported with another tag counts as that kind only.
TEST(RingState, AStreamIsOneKindAtATime)
{
    RingState state;
    state.streamSeen(3, "pmedia", false);
    EXPECT_TRUE(state.streamSeen(3, "pnotifications", false));
    EXPECT_FALSE(state.mediaPlaying());
    EXPECT_TRUE(state.alerting());
    EXPECT_TRUE(state.streamSeen(3, "pringtones", false));
    EXPECT_FALSE(state.alerting());
    EXPECT_TRUE(state.ringing());
}

// An alert during a call stays in the ear, as a ring does.
TEST(RingState, AnAlertInACallStaysInTheEar)
{
    RingState state;
    state.streamSeen(5, "palerts", false);
    EXPECT_TRUE(RingState::speakerToo(eCallMode_None, state.speakerWanted(true)));
    EXPECT_FALSE(RingState::speakerToo(eCallMode_Carrier, state.speakerWanted(true)));
    EXPECT_FALSE(RingState::speakerToo(eCallMode_Voip, state.speakerWanted(true)));
}

} // namespace
