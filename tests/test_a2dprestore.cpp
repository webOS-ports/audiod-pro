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

#include "a2dpRestore.h"

namespace {

const std::string kAdapter = "02:00:00:00:00:02";
const std::string kHeadset = "02:00:00:00:00:01";
const std::string kSpeaker = "aa:bb:cc:dd:ee:02";

using Devices = std::vector<std::pair<std::string, std::string>>;

// What the Mindphone did on 2026-10-31 06:45:23 (kill -9 of PulseAudio
// while music played to the Nuraphone): audiod lost PulseAudio at 23.771,
// bluetooth2 reported the headset's A2DP gone at 24.940, PulseAudio was
// back by 25.3. The headset is connected again once PulseAudio is back.
TEST(A2dpRestore, HeadsetLosingA2dpWithPulseAudioIsConnectedAgain)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 23771);
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 24940));
    // Not while PulseAudio is away: BlueZ has no endpoint to configure.
    EXPECT_TRUE(restore.due().empty());
    EXPECT_TRUE(restore.pending());
    EXPECT_TRUE(restore.pulseBack());
    EXPECT_EQ(restore.due(), (Devices{{kHeadset, kAdapter}}));
    // Handed out once.
    EXPECT_TRUE(restore.due().empty());
    EXPECT_FALSE(restore.pending());
    EXPECT_TRUE(restore.wanted(kHeadset));
    restore.done(kHeadset);
    EXPECT_FALSE(restore.wanted(kHeadset));
}

// PulseAudio can be back before bluetooth2 reports the drop.
TEST(A2dpRestore, DropReportedAfterPulseAudioIsBack)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    EXPECT_FALSE(restore.pulseBack());
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 2500));
    EXPECT_EQ(restore.due(), (Devices{{kHeadset, kAdapter}}));
}

// A drop long after PulseAudio went is the headset's own (or the user's).
TEST(A2dpRestore, LateDropIsNotPulseAudios)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    restore.pulseBack();
    EXPECT_FALSE(restore.a2dpGone(kHeadset, 1000 + A2dpRestore::DROP_WINDOW_MS + 1));
    EXPECT_TRUE(restore.due().empty());
    EXPECT_FALSE(restore.wanted(kHeadset));
}

TEST(A2dpRestore, DropAtTheEndOfTheWindowCounts)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 1000 + A2dpRestore::DROP_WINDOW_MS));
}

// A clock going backwards (it should not) is no reason to connect.
TEST(A2dpRestore, DropBeforePulseAudioWentDoesNotCount)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 5000);
    EXPECT_FALSE(restore.a2dpGone(kHeadset, 4999));
}

// Only the headsets that had A2DP when PulseAudio went.
TEST(A2dpRestore, OnlyHeadsetsConnectedWhenPulseAudioWent)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    EXPECT_FALSE(restore.a2dpGone(kSpeaker, 1500));
    restore.pulseBack();
    EXPECT_TRUE(restore.due().empty());
}

// Without PulseAudio going, a drop is never one to undo.
TEST(A2dpRestore, NothingWithoutPulseAudioGoing)
{
    A2dpRestore restore;
    EXPECT_FALSE(restore.a2dpGone(kHeadset, 1000));
    EXPECT_FALSE(restore.pending());
    EXPECT_TRUE(restore.due().empty());
}

// Each headset counts once per PulseAudio going: dropping, coming back by
// itself and dropping again is not undone a second time.
TEST(A2dpRestore, SecondDropIsNotUndone)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 1500));
    restore.a2dpBack(kHeadset);
    EXPECT_FALSE(restore.wanted(kHeadset));
    EXPECT_FALSE(restore.a2dpGone(kHeadset, 1800));
    restore.pulseBack();
    EXPECT_TRUE(restore.due().empty());
}

// A drop reported twice for one going (bluetooth2 repeating itself, the
// attempt done meanwhile) is undone once: the first report takes the
// headset off the list of those PulseAudio's going took.
TEST(A2dpRestore, DropReportedTwiceIsUndoneOnce)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 1500));
    restore.pulseBack();
    ASSERT_EQ(restore.due().size(), 1u);
    restore.done(kHeadset);
    EXPECT_FALSE(restore.a2dpGone(kHeadset, 1800));
    EXPECT_FALSE(restore.pending());
}

// A headset that has A2DP again by itself needs no attempt.
TEST(A2dpRestore, HeadsetBackByItselfIsLeftAlone)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 2000));
    restore.a2dpBack(kHeadset);
    EXPECT_FALSE(restore.pulseBack());
    EXPECT_TRUE(restore.due().empty());
}

// A failed attempt is made again, up to MAX_ATTEMPTS in all.
TEST(A2dpRestore, FailedAttemptIsRetriedThenGivenUp)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    restore.a2dpGone(kHeadset, 2000);
    restore.pulseBack();
    int attempts = 0;
    for (int i = 0; i < 10; i++)
    {
        Devices due = restore.due();
        if (due.empty())
            break;
        attempts++;
        restore.failed(kHeadset);
    }
    EXPECT_EQ(attempts, A2dpRestore::MAX_ATTEMPTS);
    EXPECT_FALSE(restore.pending());
    EXPECT_FALSE(restore.wanted(kHeadset));
}

TEST(A2dpRestore, FailedSaysWhetherAnotherAttemptIsLeft)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    restore.a2dpGone(kHeadset, 2000);
    restore.pulseBack();
    ASSERT_EQ(restore.due().size(), 1u);
    EXPECT_TRUE(restore.failed(kHeadset));
    EXPECT_TRUE(restore.pending());
    ASSERT_EQ(restore.due().size(), 1u);
    EXPECT_FALSE(restore.failed(kHeadset));
    EXPECT_FALSE(restore.failed(kSpeaker));
}

// PulseAudio going again before the attempt: it waits for it to be back.
TEST(A2dpRestore, PulseAudioGoingAgainHoldsTheAttempt)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    restore.a2dpGone(kHeadset, 2000);
    restore.pulseBack();
    restore.pulseGone({}, 3000);
    EXPECT_TRUE(restore.due().empty());
    EXPECT_TRUE(restore.pulseBack());
    EXPECT_EQ(restore.due(), (Devices{{kHeadset, kAdapter}}));
}

// Going twice before it was back keeps those the first going took.
TEST(A2dpRestore, GoingTwiceKeepsTheFirstHeadsets)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    restore.pulseGone({{kSpeaker, kAdapter}}, 1200);
    EXPECT_TRUE(restore.a2dpGone(kHeadset, 1900));
    EXPECT_TRUE(restore.a2dpGone(kSpeaker, 1950));
    restore.pulseBack();
    EXPECT_EQ(restore.due(), (Devices{{kHeadset, kAdapter}, {kSpeaker, kAdapter}}));
}

// A new going, after it was back, starts over.
TEST(A2dpRestore, NewGoingForgetsTheLastOne)
{
    A2dpRestore restore;
    restore.pulseGone({{kHeadset, kAdapter}}, 1000);
    restore.pulseBack();
    restore.pulseGone({{kSpeaker, kAdapter}}, 2000);
    EXPECT_FALSE(restore.a2dpGone(kHeadset, 2500));
    EXPECT_TRUE(restore.a2dpGone(kSpeaker, 2500));
}

TEST(A2dpRestore, EmptyAddressIsIgnored)
{
    A2dpRestore restore;
    restore.pulseGone({{"", kAdapter}}, 1000);
    EXPECT_FALSE(restore.a2dpGone("", 1500));
}

} // namespace
