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

TEST(DuckPolicy, DuckedIsSeventyPercentOtherwiseFull)
{
    EXPECT_EQ(DuckPolicy::targetPercent(true), 70);
    EXPECT_EQ(DuckPolicy::targetPercent(false), 100);
}

TEST(DuckPolicy, TheRampStepsDownAndStopsAtTheTarget)
{
    EXPECT_EQ(DuckPolicy::nextPercent(100, 70), 95);
    EXPECT_EQ(DuckPolicy::nextPercent(72, 70), 70);
    EXPECT_EQ(DuckPolicy::nextPercent(70, 70), 70);
}

TEST(DuckPolicy, TheRampStepsUpAndStopsAtTheTarget)
{
    EXPECT_EQ(DuckPolicy::nextPercent(70, 100), 75);
    EXPECT_EQ(DuckPolicy::nextPercent(98, 100), 100);
}

TEST(DuckPolicy, TheRampReachesTheTargetInAFewSteps)
{
    int percent = 100, steps = 0;
    while (percent != 70 && steps < 100)
    {
        percent = DuckPolicy::nextPercent(percent, 70);
        ++steps;
    }
    EXPECT_EQ(percent, 70);
    EXPECT_EQ(steps, 6);
}

TEST(DuckPolicy, AVolumeIsScaledByTheShare)
{
    EXPECT_EQ(DuckPolicy::scaled(100, 70), 70);
    EXPECT_EQ(DuckPolicy::scaled(80, 70), 56);
    EXPECT_EQ(DuckPolicy::scaled(100, 100), 100);
}
