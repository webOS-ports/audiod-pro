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

#include "duckPolicy.h"

using DuckPolicy::ActiveStream;

TEST(DuckPolicy, MusicAloneIsNotLowered)
{
    EXPECT_FALSE(DuckPolicy::shouldDuck({{"pdefaultapp", false}}, true));
}

TEST(DuckPolicy, AnAlertLowersTheMusic)
{
    EXPECT_TRUE(DuckPolicy::shouldDuck({{"pdefaultapp", false}, {"palerts", false}}, true));
}

TEST(DuckPolicy, ARingtoneLowersTheMusic)
{
    EXPECT_TRUE(DuckPolicy::shouldDuck({{"pdefaultapp", false}, {"pringtones", false}}, true));
}

TEST(DuckPolicy, AKeyClickDoesNotLowerTheMusic)
{
    EXPECT_FALSE(DuckPolicy::shouldDuck({{"pdefaultapp", false}, {"pfeedback", false}}, true));
}

TEST(DuckPolicy, AMutedAlertDoesNotLowerTheMusic)
{
    EXPECT_FALSE(DuckPolicy::shouldDuck({{"pdefaultapp", false}, {"palerts", true}}, true));
}

TEST(DuckPolicy, TheRingerSwitchOffKeepsTheMusicUp)
{
    EXPECT_FALSE(DuckPolicy::shouldDuck({{"pdefaultapp", false}, {"palerts", false}}, false));
}

TEST(DuckPolicy, OneAudibleAlertAmongMutedOnesLowersTheMusic)
{
    EXPECT_TRUE(DuckPolicy::shouldDuck(
        {{"pringtones", true}, {"palerts", false}, {"pdefaultapp", false}}, true));
}

TEST(DuckPolicy, TheMusicStreamsAreTheOnesLowered)
{
    EXPECT_TRUE(DuckPolicy::isDuckable("pdefaultapp"));
    EXPECT_TRUE(DuckPolicy::isDuckable("pmedia"));
    EXPECT_TRUE(DuckPolicy::isDuckable("btstream"));
    EXPECT_FALSE(DuckPolicy::isDuckable("palerts"));
    EXPECT_FALSE(DuckPolicy::isDuckable("pfeedback"));
    EXPECT_FALSE(DuckPolicy::isDuckable("voipcall"));
}

TEST(DuckPolicy, DbAreVolumeStepsOnPalmPolicysTable)
{
    // palm-policy table 0: 70 is -12.12 dB, 50 is -20.20, 65 is -14.14.
    EXPECT_EQ(DuckPolicy::levelForDb(-12.0), 30);
    EXPECT_EQ(DuckPolicy::levelForDb(-14.0), 35);
    EXPECT_EQ(DuckPolicy::levelForDb(-20.0), 50);
    EXPECT_EQ(DuckPolicy::levelForDb(0.0), 0);
}

TEST(DuckPolicy, APositiveDbIsReadAsItsNegative)
{
    EXPECT_EQ(DuckPolicy::levelForDb(20.0), 50);
}

TEST(DuckPolicy, TheLevelStopsShortOfSilence)
{
    EXPECT_EQ(DuckPolicy::levelForDb(-100.0), 98);
}

TEST(DuckPolicy, TheDefaultIsTwentyDb)
{
    EXPECT_EQ(DuckPolicy::levelForDb(DuckPolicy::kDefaultDuckDb), 50);
}

TEST(DuckPolicy, DuckedIsTheDepthOtherwiseNothing)
{
    EXPECT_EQ(DuckPolicy::targetLevel(true, 50), 50);
    EXPECT_EQ(DuckPolicy::targetLevel(false, 50), 0);
}

TEST(DuckPolicy, AFullDuckTakesSixSteps)
{
    for (int depth : {30, 35, 50, 98})
    {
        const int step = DuckPolicy::rampStep(depth);
        int level = 0, steps = 0;
        while (level != depth && steps < 100)
        {
            level = DuckPolicy::nextLevel(level, depth, step);
            ++steps;
        }
        EXPECT_EQ(level, depth) << depth;
        EXPECT_EQ(steps, DuckPolicy::kRampSteps) << depth;
    }
}

TEST(DuckPolicy, ATinyDuckStillMoves)
{
    EXPECT_EQ(DuckPolicy::rampStep(1), 1);
    EXPECT_EQ(DuckPolicy::rampStep(0), 1);
}

TEST(DuckPolicy, TheRampStepsDownAndStopsAtTheTarget)
{
    EXPECT_EQ(DuckPolicy::nextLevel(0, 50, 9), 9);
    EXPECT_EQ(DuckPolicy::nextLevel(45, 50, 9), 50);
    EXPECT_EQ(DuckPolicy::nextLevel(50, 50, 9), 50);
}

TEST(DuckPolicy, TheRampStepsUpAndStopsAtTheTarget)
{
    EXPECT_EQ(DuckPolicy::nextLevel(50, 0, 9), 41);
    EXPECT_EQ(DuckPolicy::nextLevel(4, 0, 9), 0);
}

TEST(DuckPolicy, AVolumeGoesDownByTheSameStepsAtAnyLevel)
{
    // The same number of steps is the same number of dB on table 0.
    EXPECT_EQ(DuckPolicy::duckedVolume(100, 50), 50);
    EXPECT_EQ(DuckPolicy::duckedVolume(80, 50), 30);
    EXPECT_EQ(DuckPolicy::duckedVolume(100, 0), 100);
}

TEST(DuckPolicy, AQuietStreamStaysAudible)
{
    EXPECT_EQ(DuckPolicy::duckedVolume(30, 50), 1);
    EXPECT_EQ(DuckPolicy::duckedVolume(0, 50), 0);
}

TEST(DuckPolicy, AnAlertWaitsForTheWholeRampWhenTheMusicIsUp)
{
    // Six steps of 30 ms, and one more for the mixer to report the stream.
    EXPECT_EQ(DuckPolicy::leadInMs("palerts", true, 0, 50), 210u);
    EXPECT_EQ(DuckPolicy::leadInMs("pringtones", true, 0, 50), 210u);
}

TEST(DuckPolicy, ASecondAlertStartsAtOnceWhenTheMusicIsDown)
{
    EXPECT_EQ(DuckPolicy::leadInMs("palerts", true, 50, 50), 0u);
}

TEST(DuckPolicy, AnAlertDuringTheRampWaitsOnlyForTheRest)
{
    // 50 steps of 9: from 27 three more ramp steps, plus the report.
    EXPECT_EQ(DuckPolicy::leadInMs("palerts", true, 27, 50), 120u);
    EXPECT_EQ(DuckPolicy::leadInMs("palerts", true, 49, 50), 60u);
}

TEST(DuckPolicy, SoundsThatDoNotDuckDoNotWait)
{
    EXPECT_EQ(DuckPolicy::leadInMs("pfeedback", true, 0, 50), 0u);
    EXPECT_EQ(DuckPolicy::leadInMs("pdefaultapp", true, 0, 50), 0u);
    EXPECT_EQ(DuckPolicy::leadInMs("palerts", false, 0, 50), 0u);
    EXPECT_EQ(DuckPolicy::leadInMs("palerts", true, 0, 0), 0u);
}

TEST(DuckPolicy, SilenceIsWholeFrames)
{
    // 210 ms of 16-bit stereo at 44.1 kHz: 9261 frames of 4 bytes.
    EXPECT_EQ(DuckPolicy::silenceBytes(210, 44100, 4), 37044u);
    EXPECT_EQ(DuckPolicy::silenceBytes(0, 44100, 4), 0u);
    EXPECT_EQ(DuckPolicy::silenceBytes(210, 44100, 4) % 4, 0u);
    // 3 ms is 132.3 frames: the part frame is left out, not cut into bytes.
    EXPECT_EQ(DuckPolicy::silenceBytes(3, 44100, 4), 528u);
}
