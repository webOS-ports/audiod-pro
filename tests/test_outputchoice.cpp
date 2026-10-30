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

#include "outputChoice.h"
#include "videoSpeaker.h"

namespace
{

// The phone's outputs as audiod_device_routing_config.json lists them, and
// the router's bookkeeping when one connects or goes.
class Phone
{
public:
    Phone()
    {
        add("pcm_output", 4, false);
        add("pcm_headphone", 3, true, "pcm_output");
        add("bluetooth_speaker0", 1, true);
        add("usb_speaker0", 2, true);
    }

    void connect(const std::string &name)
    {
        OutputChoice::Output &o = find(name);
        o.connectedAt = mOrder.stamp(o.connectedAt);
        o.connected = true;
        o.awaited = false;
    }

    void disconnect(const std::string &name)
    {
        OutputChoice::Output &o = find(name);
        o.connected = false;
        o.connectedAt = 0;
        o.awaited = false;
    }

    // PulseAudio went away: the router marks every output disconnected
    // (AudioRouter::eventMixerStatus), keeps their connect stamps, and
    // awaits the removable ones that were connected.
    void losePulseAudio()
    {
        for (auto &o : mOutputs)
        {
            o.awaited = (o.connected || o.awaited) && o.removable && o.connectedAt != 0;
            o.connected = false;
        }
    }

    // The router gave up waiting (AudioRouter::awaitTimedOut).
    void stopWaiting()
    {
        for (auto &o : mOutputs)
            if (o.awaited)
            {
                o.awaited = false;
                o.connectedAt = 0;
            }
    }

    bool waiting() const { return OutputChoice::waiting(mOutputs); }

    std::string chosen() const { return OutputChoice::choose(mOutputs); }

    // An output reported connected, as AudioRouter::setOutputDeviceRouting
    // has it: what the audio is routed to now, "" for no change.
    std::string announce(const std::string &name)
    {
        connect(name);
        return OutputChoice::routeOnAnnounce(mOutputs, name);
    }

private:
    void add(const std::string &name, int priority, bool removable, const std::string &sink = "")
    {
        OutputChoice::Output o;
        o.name = name;
        o.priority = priority;
        o.removable = removable;
        o.sink = sink;
        mOutputs.push_back(o);
    }

    OutputChoice::Output &find(const std::string &name)
    {
        for (auto &o : mOutputs)
            if (o.name == name)
                return o;
        ADD_FAILURE() << "no output " << name;
        return mOutputs.front();
    }

    std::vector<OutputChoice::Output> mOutputs;
    OutputChoice::ConnectOrder mOrder;
};

}

TEST(OutputChoice, NothingConnectedIsNoOutput)
{
    Phone phone;
    EXPECT_EQ(phone.chosen(), "");
}

TEST(OutputChoice, TheSpeakerAloneHasTheAudio)
{
    Phone phone;
    phone.connect("pcm_output");
    EXPECT_EQ(phone.chosen(), "pcm_output");
}

TEST(OutputChoice, HeadphonesPluggedInWhileBluetoothPlaysTakeTheAudio)
{
    // B08, seen on mindphone 30 Oct: nuraphone playing, jack plugged in,
    // the music stayed on Bluetooth.
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "bluetooth_speaker0");
    phone.connect("pcm_headphone");
    EXPECT_EQ(phone.chosen(), "pcm_headphone");
}

TEST(OutputChoice, BluetoothConnectedWithHeadphonesInTakesTheAudio)
{
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("pcm_headphone");
    phone.connect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "bluetooth_speaker0");
}

TEST(OutputChoice, TheOneInUseGoingFallsBackToTheOneConnectedBefore)
{
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    phone.connect("pcm_headphone");
    phone.disconnect("pcm_headphone");
    EXPECT_EQ(phone.chosen(), "bluetooth_speaker0");

    phone.connect("pcm_headphone");
    phone.disconnect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "pcm_headphone");
}

TEST(OutputChoice, RemovingOneNotInUseChangesNothing)
{
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("pcm_headphone");
    phone.connect("bluetooth_speaker0");
    phone.disconnect("pcm_headphone");
    EXPECT_EQ(phone.chosen(), "bluetooth_speaker0");
}

TEST(OutputChoice, WithEveryRemovableOneGoneTheSpeakerHasIt)
{
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("pcm_headphone");
    phone.connect("bluetooth_speaker0");
    phone.disconnect("bluetooth_speaker0");
    phone.disconnect("pcm_headphone");
    EXPECT_EQ(phone.chosen(), "pcm_output");
}

TEST(OutputChoice, AReconnectMakesItTheLatestAgain)
{
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    phone.connect("pcm_headphone");
    phone.disconnect("bluetooth_speaker0");
    phone.connect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "bluetooth_speaker0");
}

TEST(OutputChoice, ReportedAgainWhileConnectedKeepsItsPlace)
{
    // PulseAudio restarting announces every sink again; the Bluetooth sink
    // coming back must not take the audio from headphones plugged in later.
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    phone.connect("pcm_headphone");
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "pcm_headphone");
}

TEST(OutputChoice, AUsbHeadsetIsRemovableToo)
{
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("pcm_headphone");
    phone.connect("usb_speaker0");
    EXPECT_EQ(phone.chosen(), "usb_speaker0");
}

TEST(OutputChoice, WithoutRemovableOnesTheBestPriorityWins)
{
    std::vector<OutputChoice::Output> outputs(2);
    outputs[0].name = "pcm_output";
    outputs[0].priority = 4;
    outputs[0].connected = true;
    outputs[1].name = "pcm_hdmi";
    outputs[1].priority = 2;
    outputs[1].connected = true;
    EXPECT_EQ(OutputChoice::choose(outputs), "pcm_hdmi");
    outputs[1].connected = false;
    EXPECT_EQ(OutputChoice::choose(outputs), "pcm_output");
}

TEST(OutputChoice, StampsOnlyGoUp)
{
    OutputChoice::ConnectOrder order;
    const std::uint64_t first = order.stamp(0);
    const std::uint64_t second = order.stamp(0);
    EXPECT_GT(second, first);
    EXPECT_EQ(order.stamp(first), first);
    // Never stamped, or the stamp dropped by a disconnect: a new one.
    EXPECT_GT(order.stamp(0), second);
}

TEST(OutputChoice, APulseAudioRestartKeepsTheOrder)
{
    // Headphones plugged in after the Bluetooth headset have the audio.
    // PulseAudio restarts: pcm_output is announced again first, the jack
    // is told again, and the headset's sink comes back last, once its
    // module is loaded again. The headphones keep the audio.
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    phone.connect("pcm_headphone");
    phone.losePulseAudio();
    EXPECT_EQ(phone.chosen(), "");
    phone.connect("pcm_output");
    phone.connect("pcm_headphone");
    phone.connect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "pcm_headphone");

    // The other way round too: the headset connected last keeps it, even
    // with the jack told again before the headset's sink is back.
    Phone other;
    other.connect("pcm_output");
    other.connect("pcm_headphone");
    other.connect("usb_speaker0");
    other.losePulseAudio();
    other.connect("pcm_headphone");
    other.connect("pcm_output");
    other.connect("usb_speaker0");
    EXPECT_EQ(other.chosen(), "usb_speaker0");
}

TEST(OutputChoice, AHeadsetDisconnectedDuringARestartIsNewWhenItComesBack)
{
    // The Bluetooth headset goes while PulseAudio is away (BluetoothManager
    // tells the router, which drops its stamp) and is connected again after
    // the headphones are back: it was connected last, so it has the audio.
    Phone phone;
    phone.connect("pcm_output");
    phone.connect("bluetooth_speaker0");
    phone.connect("pcm_headphone");
    phone.losePulseAudio();
    phone.disconnect("bluetooth_speaker0");
    phone.connect("pcm_output");
    phone.connect("pcm_headphone");
    EXPECT_EQ(phone.chosen(), "pcm_headphone");
    phone.connect("bluetooth_speaker0");
    EXPECT_EQ(phone.chosen(), "bluetooth_speaker0");
}

// With the headset connected last carrying the audio, a video call (test
// plan E04, VideoSpeaker) must still see a headset whenever one is there.
// The legacy manager counts the jack, or Bluetooth while it carries the
// audio, and tells the router about the jack before it re-routes the call,
// so the router's choice afterwards is what VideoSpeaker is given.
namespace
{

struct VideoCallOnPhone
{
    Phone phone;
    bool jack = false;
    VideoSpeaker policy;
    bool speaker = false;

    VideoSpeaker::Move route()
    {
        const bool headset = jack || phone.chosen() == "bluetooth_speaker0";
        const VideoSpeaker::Move move = policy.update({true, !speaker, speaker, headset, false});
        if (move == VideoSpeaker::eMove_Speaker)
            speaker = true;
        else if (move == VideoSpeaker::eMove_Earpiece)
            speaker = false;
        return move;
    }

    void plug(bool in)
    {
        jack = in;
        if (in)
            phone.connect("pcm_headphone");
        else
            phone.disconnect("pcm_headphone");
    }
};

}

TEST(OutputChoice, AVideoCallStaysInTheHeadsetsWhicheverWasConnectedLast)
{
    VideoCallOnPhone call;
    call.phone.connect("pcm_output");
    call.phone.connect("bluetooth_speaker0");
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_None);

    // Headphones in after the Bluetooth headset: they have the audio now,
    // and the call is still in a headset.
    call.plug(true);
    EXPECT_EQ(call.phone.chosen(), "pcm_headphone");
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_None);

    // Out again: the Bluetooth headset takes over, no loudspeaker.
    call.plug(false);
    EXPECT_EQ(call.phone.chosen(), "bluetooth_speaker0");
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_None);
    EXPECT_FALSE(call.speaker);

    // Bluetooth gone as well: now it is the loudspeaker.
    call.phone.disconnect("bluetooth_speaker0");
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_Speaker);
    EXPECT_TRUE(call.speaker);
}

TEST(OutputChoice, AHeadsetArrivingInAVideoCallTakesItOffTheLoudspeaker)
{
    VideoCallOnPhone call;
    call.phone.connect("pcm_output");
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_Speaker);

    call.plug(true);
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_Earpiece);

    // Bluetooth connected after the headphones: still a headset.
    call.phone.connect("bluetooth_speaker0");
    EXPECT_EQ(call.route(), VideoSpeaker::eMove_None);
    EXPECT_FALSE(call.speaker);
}

TEST(OutputChoice, AnnouncingTheOutputThatWinsRoutesToIt)
{
    Phone phone;
    EXPECT_EQ(phone.announce("pcm_output"), "pcm_output");
    EXPECT_EQ(phone.announce("bluetooth_speaker0"), "bluetooth_speaker0");
    // Reported again while it has the audio: routed to again.
    EXPECT_EQ(phone.announce("bluetooth_speaker0"), "bluetooth_speaker0");
    // pcm_output reported again while Bluetooth has the audio: no change.
    EXPECT_EQ(phone.announce("pcm_output"), "");
}

TEST(OutputChoice, PcmOutputBackAfterARestartCarriesTheHeadphones)
{
    // Headphones in, PulseAudio restarts. The legacy manager tells the
    // router about the jack again as soon as PulseAudio is back, before
    // pcm_output is there to route to; when pcm_output is announced, the
    // headphones play through it and it has to be routed to then.
    Phone phone;
    phone.announce("pcm_output");
    EXPECT_EQ(phone.announce("pcm_headphone"), "pcm_headphone");
    phone.losePulseAudio();
    EXPECT_EQ(phone.announce("pcm_headphone"), "pcm_headphone");
    EXPECT_EQ(phone.announce("pcm_output"), "pcm_headphone");
    EXPECT_EQ(phone.chosen(), "pcm_headphone");

    // Without the jack told again, the audio would wait for the
    // headphones (TheAudioWaitsForTheHeadsetAfterARestart) until the
    // router gives up on them, and then a Bluetooth headset coming back
    // would take it from the headphones it was connected before.
    Phone untold;
    untold.announce("pcm_output");
    untold.announce("bluetooth_speaker0");
    untold.announce("pcm_headphone");
    untold.losePulseAudio();
    EXPECT_EQ(untold.announce("pcm_output"), "");
    EXPECT_EQ(untold.announce("bluetooth_speaker0"), "");
    untold.stopWaiting();
    EXPECT_EQ(untold.chosen(), "bluetooth_speaker0");
}

TEST(OutputChoice, ASinkTheChosenOneDoesNotPlayThroughChangesNothing)
{
    Phone phone;
    phone.announce("pcm_output");
    phone.announce("bluetooth_speaker0");
    phone.announce("pcm_headphone");
    // The headphones have it; Bluetooth's sink reported again is no reason
    // to route anywhere.
    EXPECT_EQ(phone.announce("bluetooth_speaker0"), "");
    EXPECT_EQ(phone.announce("usb_speaker0"), "usb_speaker0");
    EXPECT_EQ(phone.announce("pcm_output"), "");
}

TEST(OutputChoice, TheAudioWaitsForTheHeadsetAfterARestart)
{
    // Music (or a video call) in a Bluetooth headset, PulseAudio restarts:
    // pcm_output is announced again before the headset's sink. Routing to
    // it then would put the audio on the loudspeaker until the headset is
    // back, and the legacy manager would take the headset for gone.
    Phone phone;
    phone.announce("pcm_output");
    phone.announce("bluetooth_speaker0");
    phone.losePulseAudio();
    EXPECT_TRUE(phone.waiting());
    EXPECT_EQ(phone.announce("pcm_output"), "");
    EXPECT_EQ(phone.announce("bluetooth_speaker0"), "bluetooth_speaker0");
    EXPECT_FALSE(phone.waiting());

    // The same for a USB headset.
    Phone usb;
    usb.announce("pcm_output");
    usb.announce("usb_speaker0");
    usb.losePulseAudio();
    EXPECT_EQ(usb.announce("pcm_output"), "");
    EXPECT_EQ(usb.announce("usb_speaker0"), "usb_speaker0");

    // Whenever pcm_output was announced first: the speaker is no removable
    // output, its own connect stamp does not count.
    Phone early;
    early.announce("bluetooth_speaker0");
    early.announce("pcm_output");
    EXPECT_EQ(early.chosen(), "bluetooth_speaker0");
    early.losePulseAudio();
    EXPECT_EQ(early.announce("pcm_output"), "");
}

TEST(OutputChoice, OnlyAnOutputThatWouldWinIsWaitedFor)
{
    // Headphones plugged in after the headset: told again at once, they
    // have the audio, and the headset coming back later changes nothing.
    Phone phone;
    phone.announce("pcm_output");
    phone.announce("bluetooth_speaker0");
    phone.announce("pcm_headphone");
    phone.losePulseAudio();
    EXPECT_EQ(phone.announce("pcm_headphone"), "pcm_headphone");
    EXPECT_FALSE(phone.waiting());
    EXPECT_EQ(phone.announce("pcm_output"), "pcm_headphone");
    EXPECT_EQ(phone.announce("bluetooth_speaker0"), "");

    // The headset connected after the headphones is waited for, also with
    // the headphones back already.
    Phone later;
    later.announce("pcm_output");
    later.announce("pcm_headphone");
    later.announce("bluetooth_speaker0");
    later.losePulseAudio();
    EXPECT_EQ(later.announce("pcm_headphone"), "");
    EXPECT_EQ(later.announce("pcm_output"), "");
    EXPECT_EQ(later.announce("bluetooth_speaker0"), "bluetooth_speaker0");

    // With only the speaker there is nothing to wait for.
    Phone speaker;
    speaker.announce("pcm_output");
    speaker.losePulseAudio();
    EXPECT_FALSE(speaker.waiting());
    EXPECT_EQ(speaker.announce("pcm_output"), "pcm_output");
}

TEST(OutputChoice, AHeadsetGoneOrNotBackIsNotWaitedFor)
{
    // Disconnected while PulseAudio was away: not waited for.
    Phone gone;
    gone.announce("pcm_output");
    gone.announce("bluetooth_speaker0");
    gone.losePulseAudio();
    gone.disconnect("bluetooth_speaker0");
    EXPECT_FALSE(gone.waiting());
    EXPECT_EQ(gone.announce("pcm_output"), "pcm_output");

    // Not back in time: the router stops waiting, the speaker has it, and
    // the headset counts as new when it does come.
    Phone late;
    late.announce("pcm_output");
    late.announce("bluetooth_speaker0");
    late.announce("pcm_headphone");
    late.losePulseAudio();
    EXPECT_EQ(late.announce("pcm_output"), "");
    late.stopWaiting();
    EXPECT_FALSE(late.waiting());
    EXPECT_EQ(late.chosen(), "pcm_output");
    EXPECT_EQ(late.announce("pcm_headphone"), "pcm_headphone");
    EXPECT_EQ(late.announce("bluetooth_speaker0"), "bluetooth_speaker0");

    // A second restart before the first one's outputs are back still
    // waits for them.
    Phone twice;
    twice.announce("pcm_output");
    twice.announce("bluetooth_speaker0");
    twice.losePulseAudio();
    twice.losePulseAudio();
    EXPECT_TRUE(twice.waiting());
}
