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

// Arguments: what was asked for, the volume manager's active output,
// whether what was asked for is connected, and whether a Bluetooth headset
// carries the audio (the legacy manager's view).

TEST(VolumeTarget, ThePhonesOwnOutputStaysWhenItCarriesTheAudio)
{
    EXPECT_EQ(VolumeTarget::output("pcm_output", "pcm_output", true, false), "pcm_output");
    // Nothing active at all yet: still the phone's own.
    EXPECT_EQ(VolumeTarget::output("pcm_output", "", true, false), "pcm_output");
}

TEST(VolumeTarget, ThePhonesOwnOutputMeansTheHeadsetWhenItCarriesTheAudio)
{
    EXPECT_EQ(VolumeTarget::output("pcm_output", "bluetooth_speaker0", true, true),
              "bluetooth_speaker0");
}

TEST(VolumeTarget, TheHeadsetConnectedBeforeAudiodStartedGetsTheVolume)
{
    // The volume manager missed the headset becoming active: no active output.
    EXPECT_EQ(VolumeTarget::output("pcm_output", "", true, true), "bluetooth_speaker0");
    EXPECT_EQ(VolumeTarget::output("pcm_output", "pcm_output", true, true), "bluetooth_speaker0");
}

TEST(VolumeTarget, BluetoothKeepsTheVolumeWithAWiredHeadsetInToo)
{
    EXPECT_EQ(VolumeTarget::output("pcm_output", "pcm_headphone", true, true),
              "bluetooth_speaker0");
}

TEST(VolumeTarget, TheVolumeManagersActiveHeadsetAloneStillCounts)
{
    // Without the legacy manager (or before it heard), its flag decides.
    EXPECT_EQ(VolumeTarget::output("pcm_output", "bluetooth_speaker0", true, false),
              "bluetooth_speaker0");
}

TEST(VolumeTarget, TheHeadsetNamedWhileItCarriesTheAudioIsTheHeadset)
{
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "bluetooth_speaker0", true, true),
              "bluetooth_speaker0");
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "bluetooth_speaker0", true, false),
              "bluetooth_speaker0");
    // Not known as connected here, but it is carrying the audio.
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "", false, true), "bluetooth_speaker0");
}

TEST(VolumeTarget, TheHeadsetNamedWhileConnectedIsTheHeadset)
{
    // Connected, not playing: a volume for it is still its volume.
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "pcm_output", true, false),
              "bluetooth_speaker0");
}

TEST(VolumeTarget, TheHeadsetNamedAfterItWentIsTheActiveOutput)
{
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "pcm_output", false, false), "pcm_output");
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "pcm_headphone", false, false),
              "pcm_headphone");
}

TEST(VolumeTarget, TheHeadsetNamedAfterItWentWithNothingActiveIsThePhonesOwn)
{
    EXPECT_EQ(VolumeTarget::output("bluetooth_speaker0", "", false, false), "pcm_output");
}

TEST(VolumeTarget, OtherOutputsAreLeftAsAsked)
{
    // A named wired or USB output, or the second display's, is that output.
    EXPECT_EQ(VolumeTarget::output("pcm_headphone", "bluetooth_speaker0", true, true), "pcm_headphone");
    EXPECT_EQ(VolumeTarget::output("pcm_output1", "bluetooth_speaker0", true, true), "pcm_output1");
    EXPECT_EQ(VolumeTarget::output("alsa", "bluetooth_speaker0", true, true), "alsa");
}

TEST(VolumeTarget, KnowsTheBluetoothOutputs)
{
    EXPECT_TRUE(VolumeTarget::isBluetoothOutput(VolumeTarget::kBluetoothOutput));
    EXPECT_TRUE(VolumeTarget::isBluetoothOutput("bluetooth1"));
    EXPECT_FALSE(VolumeTarget::isBluetoothOutput("pcm_output"));
    EXPECT_FALSE(VolumeTarget::isBluetoothOutput("bluez_sink.74_1A_E0_20_8F_18.a2dp_sink"));
    EXPECT_FALSE(VolumeTarget::isBluetoothOutput(""));
}
