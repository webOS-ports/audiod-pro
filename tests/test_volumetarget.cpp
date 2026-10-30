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

#include "volumeTarget.h"

TEST(VolumeTarget, ThePhonesOwnOutputStaysWhenItIsActive)
{
    EXPECT_EQ(VolumeTarget::output("pcm_output", "pcm_output", true), "pcm_output");
}

TEST(VolumeTarget, ThePhonesOwnOutputMeansTheHeadsetWhileBluetoothIsActive)
{
    EXPECT_EQ(VolumeTarget::output("pcm_output", "bluetooth_speaker0", true),
              "bluetooth_speaker0");
}

TEST(VolumeTarget, TheHeadsetNamedWhileConnectedIsTheHeadset)
{
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "bluetooth_speaker0", true),
              "bluetooth_speaker0");
}

TEST(VolumeTarget, TheHeadsetNamedAfterItWentIsTheActiveOutput)
{
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "pcm_output", false), "pcm_output");
}

TEST(VolumeTarget, TheHeadsetNamedWithNothingActiveIsLeftAsAsked)
{
    // Nothing to send it to: the request fails as it did, not on "".
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "", false), "bluetooth_speaker0");
}

TEST(VolumeTarget, OtherOutputsAreLeftAsAsked)
{
    // A named wired or USB output, or the second display's, is that output.
    EXPECT_EQ(VolumeTarget::output("pcm_headphone", "bluetooth_speaker0", true), "pcm_headphone");
    EXPECT_EQ(VolumeTarget::output("pcm_output1", "bluetooth_speaker0", true), "pcm_output1");
    EXPECT_EQ(VolumeTarget::output("alsa", "bluetooth_speaker0", true), "alsa");
}

TEST(VolumeTarget, KnowsTheBluetoothOutputs)
{
    EXPECT_TRUE(VolumeTarget::isBluetoothOutput("bluetooth_speaker0"));
    EXPECT_TRUE(VolumeTarget::isBluetoothOutput("bluetooth1"));
    EXPECT_FALSE(VolumeTarget::isBluetoothOutput("pcm_output"));
    EXPECT_FALSE(VolumeTarget::isBluetoothOutput("bluez_sink.74_1A_E0_20_8F_18.a2dp_sink"));
    EXPECT_FALSE(VolumeTarget::isBluetoothOutput(""));
}
