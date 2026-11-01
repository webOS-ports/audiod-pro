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

#include <pulse/module-palm-policy.h>

#include "classRoute.h"
#include "usbRing.h"

// USB-RING: with a USB headset carrying the audio, the ringtones and alerts
// play on the loudspeaker and the headset, as legacy webOS did for a wired
// headset; media stays in the headset, calls on their own route.

namespace {

typedef UsbRing::Step Step;

TEST(UsbRing, TheClassesLegacyRangOnBoth)
{
    // ringer on: palerts/pnotifications/pcalendar (alerts) and the
    // ringtones with palarm
    EXPECT_EQ(UsbRing::classes(eCallMode_None, true), (std::vector<int>{ealerts, eringtones}));
    // ringer off: the alarm (with the muted ringtone) only
    EXPECT_EQ(UsbRing::classes(eCallMode_None, false), (std::vector<int>{eringtones}));
}

TEST(UsbRing, NothingOnTheLoudspeakerInACall)
{
    EXPECT_TRUE(UsbRing::classes(eCallMode_Carrier, true).empty());
    EXPECT_TRUE(UsbRing::classes(eCallMode_Voip, true).empty());
}

TEST(UsbRing, NeitherMediaNorTheCallClasses)
{
    for (bool ringer : {true, false})
        for (int c : UsbRing::classes(eCallMode_None, ringer))
            EXPECT_TRUE(c == ealerts || c == eringtones) << c;
}

TEST(UsbRing, ModuleArgumentsCombineTheSpeakerAndTheHeadset)
{
    EXPECT_EQ(UsbRing::moduleArguments("usb_speaker0"),
              "sink_name=ring_speaker_usb slaves=pcm_output,usb_speaker0");
    EXPECT_EQ(UsbRing::moduleArguments("usb_speaker1"),
              "sink_name=ring_speaker_usb slaves=pcm_output,usb_speaker1");
}

TEST(UsbRing, NoArgumentsForANameThatIsNoUsbSinkOrUnsafe)
{
    EXPECT_EQ(UsbRing::moduleArguments(""), "");
    EXPECT_EQ(UsbRing::moduleArguments("pcm_output"), "");
    EXPECT_EQ(UsbRing::moduleArguments("bluez_sink.00_11.a2dp_sink"), "");
    EXPECT_EQ(UsbRing::moduleArguments("usb_speaker0,pdefaultapp"), "");
    EXPECT_EQ(UsbRing::moduleArguments("usb_speaker0 sink_name=x"), "");
    EXPECT_EQ(UsbRing::moduleArguments("usb_speaker0'"), "");
}

TEST(UsbRing, NothingBeforePulseAudioIsChecked)
{
    UsbRing r;
    r.outputIs("usb_speaker0");
    EXPECT_EQ(r.next(), Step::None);
    EXPECT_FALSE(r.ready());
    r.checked();
    EXPECT_EQ(r.next(), Step::Load);
}

TEST(UsbRing, LoadedForTheHeadsetCarryingTheAudio)
{
    UsbRing r;
    r.checked();
    r.outputIs("pcm_output");
    EXPECT_EQ(r.next(), Step::None);
    r.outputIs("usb_speaker0");
    ASSERT_EQ(r.next(), Step::Load);
    EXPECT_EQ(r.wanted(), "usb_speaker0");
    r.loading();
    EXPECT_EQ(r.next(), Step::None);            // one at a time
    EXPECT_FALSE(r.ready());
    r.loaded(17);
    EXPECT_TRUE(r.ready());
    EXPECT_EQ(r.module(), 17u);
    EXPECT_EQ(r.next(), Step::None);
}

TEST(UsbRing, UnloadedWhenTheHeadsetNoLongerCarriesTheAudio)
{
    for (const char *other : {"pcm_output", "bluez_sink.00_11_22.a2dp_sink", ""})
    {
        UsbRing r;
        r.checked();
        r.outputIs("usb_speaker0");
        r.loading();
        r.loaded(17);
        r.outputIs(other);
        EXPECT_FALSE(r.ready()) << other;
        ASSERT_EQ(r.next(), Step::Unload) << other;
        EXPECT_EQ(r.module(), 17u);
        r.unloading();
        EXPECT_EQ(r.next(), Step::None) << other;
    }
}

TEST(UsbRing, AnotherHeadsetGetsASinkOfItsOwn)
{
    UsbRing r;
    r.checked();
    r.outputIs("usb_speaker0");
    r.loading();
    r.loaded(17);
    r.outputIs("usb_speaker1");
    EXPECT_FALSE(r.ready());
    ASSERT_EQ(r.next(), Step::Unload);
    r.unloading();
    ASSERT_EQ(r.next(), Step::Load);
    EXPECT_EQ(r.wanted(), "usb_speaker1");
}

TEST(UsbRing, TheHeadsetGoingWhileItLoadsUnloadsItAfter)
{
    UsbRing r;
    r.checked();
    r.outputIs("usb_speaker0");
    r.loading();
    r.outputIs("pcm_output");
    EXPECT_EQ(r.next(), Step::None);
    r.loaded(17);
    EXPECT_FALSE(r.ready());
    EXPECT_EQ(r.next(), Step::Unload);
}

TEST(UsbRing, AFailedLoadIsNotRetriedForTheSameHeadset)
{
    UsbRing r;
    r.checked();
    r.outputIs("usb_speaker0");
    r.loading();
    r.loadFailed();
    EXPECT_FALSE(r.ready());
    EXPECT_EQ(r.next(), Step::None);
    r.outputIs("usb_speaker0");                 // the router says it again
    EXPECT_EQ(r.next(), Step::None);
    r.outputIs("pcm_output");                   // unplugged, plugged again
    r.outputIs("usb_speaker0");
    EXPECT_EQ(r.next(), Step::Load);
}

TEST(UsbRing, PulseAudioGoingTakesTheSinkWithIt)
{
    UsbRing r;
    r.checked();
    r.outputIs("usb_speaker0");
    r.loading();
    r.loaded(17);
    r.paGone();
    EXPECT_FALSE(r.ready());
    EXPECT_EQ(r.next(), Step::None);            // nothing to unload, not checked yet
    r.checked();
    EXPECT_EQ(r.next(), Step::Load);            // the headset still carries the audio
}

// What the router sends while USB carries the audio: the display's range
// with the two classes split off to the combined sink.
ClassRoute::Override usbOverride(const std::vector<int> &classes)
{
    return ClassRoute::Override{"usb_speaker0", UsbRing::SINK, classes};
}

TEST(UsbRing, TheRingClassesGoToTheCombinedSinkTheRestStays)
{
    const auto r = ClassRoute::split(eVirtualSink_First, eVirtualSink_Last, "usb_speaker0",
                                     usbOverride(UsbRing::classes(eCallMode_None, true)));
    // every class routed exactly once, the two to the combined sink
    std::vector<std::string> to(eVirtualSink_Last + 1);
    int next = eVirtualSink_First;
    for (const auto &run : r)
    {
        EXPECT_EQ(run.start, next);
        ASSERT_LE(run.start, run.end);
        for (int c = run.start; c <= run.end; c++)
            to[c] = run.sink;
        next = run.end + 1;
    }
    EXPECT_EQ(next, eVirtualSink_Last + 1);
    EXPECT_EQ(to[ealerts], UsbRing::SINK);
    EXPECT_EQ(to[eringtones], UsbRing::SINK);
    EXPECT_EQ(to[efeedback], "usb_speaker0");
    EXPECT_EQ(to[emedia], "usb_speaker0");
    EXPECT_EQ(to[edefaultapp], "usb_speaker0");
    EXPECT_EQ(to[evoipcall], "usb_speaker0");
    // alerts | feedback | ringtones | the rest
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[1], (ClassRoute::Range{efeedback, efeedback, "usb_speaker0"}));
}

}
