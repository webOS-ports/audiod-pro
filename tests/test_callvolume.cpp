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

#include <cmath>
#include <set>
#include <string>

#include "callVolume.h"

// The in-call volume: one per route, moved by whole levels, kept apart from
// the master volume, and what the audio HAL is told for it.

namespace {

using Route = CallVolume::Route;

constexpr Route kAll[] = { Route::Earpiece, Route::Speaker, Route::Wired, Route::Bluetooth };

// What MediaTek's AudioALSAStreamManager::setVoiceVolume() makes of it.
int mtkIndex(double v) { return static_cast<int>(v * 5.0) + 1; }

}

TEST(CallVolume, StartsAtTheLoudestLevel)
{
    CallVolume cv;
    for (Route r : kAll)
        EXPECT_EQ(cv.volume(r), 100);
}

TEST(CallVolume, EachRouteHasItsOwn)
{
    CallVolume cv;
    EXPECT_TRUE(cv.set(Route::Earpiece, 40));
    EXPECT_TRUE(cv.set(Route::Speaker, 80));
    EXPECT_TRUE(cv.offset(Route::Bluetooth, -10));
    EXPECT_EQ(cv.volume(Route::Earpiece), 40);
    EXPECT_EQ(cv.volume(Route::Speaker), 80);
    EXPECT_EQ(cv.volume(Route::Wired), 100);
    EXPECT_EQ(cv.volume(Route::Bluetooth), 80);
}

TEST(CallVolume, AKeyIsOneLevel)
{
    CallVolume cv;
    EXPECT_TRUE(cv.offset(Route::Earpiece, -10));
    EXPECT_EQ(cv.volume(Route::Earpiece), 80);
    EXPECT_TRUE(cv.offset(Route::Earpiece, -1));
    EXPECT_EQ(cv.volume(Route::Earpiece), 60);
    EXPECT_TRUE(cv.offset(Route::Earpiece, 10));
    EXPECT_EQ(cv.volume(Route::Earpiece), 80);
    EXPECT_TRUE(cv.offset(Route::Earpiece, -21));
    EXPECT_EQ(cv.volume(Route::Earpiece), 40);
    EXPECT_FALSE(cv.offset(Route::Earpiece, 0));
    EXPECT_EQ(cv.volume(Route::Earpiece), 40);
}

TEST(CallVolume, StopsAtBothEnds)
{
    CallVolume cv;
    EXPECT_FALSE(cv.offset(Route::Speaker, 10));
    EXPECT_EQ(cv.volume(Route::Speaker), 100);
    for (int i = 0; i < CallVolume::kLevels; ++i)
        EXPECT_TRUE(cv.offset(Route::Speaker, -10));
    EXPECT_EQ(cv.volume(Route::Speaker), 0);
    EXPECT_FALSE(cv.offset(Route::Speaker, -10));
    EXPECT_FALSE(cv.offset(Route::Speaker, -1000));
    EXPECT_EQ(cv.volume(Route::Speaker), 0);
    EXPECT_TRUE(cv.offset(Route::Speaker, 1000));
    EXPECT_EQ(cv.volume(Route::Speaker), 100);
}

TEST(CallVolume, SnapsToTheNearestLevel)
{
    EXPECT_EQ(CallVolume::snap(-5), 0);
    EXPECT_EQ(CallVolume::snap(0), 0);
    EXPECT_EQ(CallVolume::snap(9), 0);
    EXPECT_EQ(CallVolume::snap(10), 20);
    EXPECT_EQ(CallVolume::snap(50), 60);
    EXPECT_EQ(CallVolume::snap(89), 80);
    EXPECT_EQ(CallVolume::snap(90), 100);
    EXPECT_EQ(CallVolume::snap(100), 100);
    EXPECT_EQ(CallVolume::snap(250), 100);

    CallVolume cv;
    EXPECT_TRUE(cv.set(Route::Wired, 47));
    EXPECT_EQ(cv.volume(Route::Wired), 40);
    EXPECT_FALSE(cv.set(Route::Wired, 41));
}

TEST(CallVolume, TheStoredVolumeIsTakenUntilTheUserSetsOne)
{
    CallVolume cv;
    EXPECT_TRUE(cv.stored(Route::Earpiece, 60));
    EXPECT_EQ(cv.volume(Route::Earpiece), 60);
    EXPECT_FALSE(cv.stored(Route::Earpiece, 60));

    EXPECT_TRUE(cv.offset(Route::Speaker, -10));
    // settingsservice answering late does not undo a key pressed before.
    EXPECT_FALSE(cv.stored(Route::Speaker, 20));
    EXPECT_EQ(cv.volume(Route::Speaker), 80);
    // A route the user did not touch still takes it.
    EXPECT_TRUE(cv.stored(Route::Wired, 33));
    EXPECT_EQ(cv.volume(Route::Wired), 40);
}

TEST(CallVolume, NamesAndKeysAreDistinct)
{
    std::set<std::string> names, keys;
    for (Route r : kAll)
    {
        names.insert(CallVolume::name(r));
        keys.insert(CallVolume::settingKey(r));
    }
    EXPECT_EQ(names.size(), 4u);
    EXPECT_EQ(keys.size(), 4u);
    EXPECT_STREQ(CallVolume::settingKey(Route::Earpiece), "callVolumeEarpiece");
    EXPECT_STREQ(CallVolume::settingKey(Route::Speaker), "callVolumeSpeaker");
    EXPECT_STREQ(CallVolume::settingKey(Route::Wired), "callVolumeWired");
    EXPECT_STREQ(CallVolume::settingKey(Route::Bluetooth), "callVolumeBluetooth");
}

TEST(CallVolume, EachLevelIsItsOwnHalIndex)
{
    // 0..100 in levels reaches MediaTek's indexes 1..6, one each, rising.
    int previous = 0;
    for (int v = 0; v <= 100; v += CallVolume::kStep)
    {
        const double linear = CallVolume::halVoiceVolume(v);
        EXPECT_GT(linear, 0.0) << v;
        EXPECT_LE(linear, 1.0) << v;
        EXPECT_EQ(mtkIndex(linear), previous + 1) << v;
        previous = mtkIndex(linear);
    }
    EXPECT_EQ(previous, 6);
    EXPECT_DOUBLE_EQ(CallVolume::halVoiceVolume(100), 1.0);
    // Off a level it is the level's.
    EXPECT_DOUBLE_EQ(CallVolume::halVoiceVolume(55), CallVolume::halVoiceVolume(60));
}

TEST(CallVolume, TheHalValueSurvivesPulseAudiosRoundTrip)
{
    // PulseAudio keeps a volume as pa_volume_t, the cube root of the linear
    // value in 1/65536ths, and pulseaudio-modules-droid gives the HAL that
    // back as linear: what arrives must still be in the same index.
    for (int v = 0; v <= 100; v += CallVolume::kStep)
    {
        const double linear = CallVolume::halVoiceVolume(v);
        const double pa = std::round(std::cbrt(linear) * 65536.0);
        const double back = std::pow(pa / 65536.0, 3.0);
        EXPECT_EQ(mtkIndex(back), mtkIndex(linear)) << v;
    }
}
