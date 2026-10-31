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

#ifndef _A2DP_RESTORE_H
#define _A2DP_RESTORE_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

/*
 * Which headsets to connect A2DP to again because PulseAudio went away,
 * kept apart from the bus so it can be tested.
 *
 * PulseAudio's module-bluez5-discover owns the A2DP endpoints BlueZ
 * streams to. When PulseAudio goes, its endpoints leave the bus with it,
 * and BlueZ 5.86 removes their stream endpoints (media.c
 * media_endpoint_remove, a2dp.c a2dp_remove_sep, avdtp.c
 * avdtp_unregister_sep): the stream is released, and the AVDTP session,
 * left without a stream or local endpoint, is closed (avdtp_unref,
 * connection_lost). The headset's A2DP is then disconnected, and
 * plugins/policy.c takes AVRCP down with it; HFP, served by oFono, stays.
 * The PulseAudio that comes up registers new endpoints, but BlueZ only
 * adds them (a2dp_add_sep): nothing connects A2DP again, so the headset
 * gets no sink and the music has nowhere to go. PipeWire covers this by
 * calling Device1.ConnectProfile for a connected device whose profiles
 * are not all up when it starts (bluez5-dbus.c reconnect_device_profiles,
 * "bluez5.auto-connect"); PulseAudio 17 does not.
 *
 * So a headset whose A2DP was connected when PulseAudio went, and which
 * loses A2DP shortly after, is connected again once PulseAudio is back.
 * Only those: a headset that drops A2DP at any other time, or long after,
 * went for a reason of its own, and one connected after PulseAudio went
 * never had a stream to lose. An attempt that fails is made again a
 * limited number of times; a headset that has A2DP again by itself (it
 * reconnected, or the user connected it) needs none.
 *
 * Times are in milliseconds of a monotonic clock, given by the caller.
 */
class A2dpRestore
{
public:
    //! How long after PulseAudio went a headset's A2DP going counts as
    //! gone with it. BlueZ takes it down within about a second (the
    //! AVDTP disconnect timer is 1 s); bluetooth2 reports it a moment on.
    static constexpr std::uint64_t DROP_WINDOW_MS = 10000;
    //! Attempts per headset, the first included.
    static constexpr int MAX_ATTEMPTS = 2;

    //! PulseAudio went away; connected are the headsets that had A2DP to
    //! us then, as (address, adapter).
    void pulseGone(const std::vector<std::pair<std::string, std::string>> &connected,
                   std::uint64_t now);
    //! PulseAudio is back. True when a headset waits to be connected.
    bool pulseBack();
    bool pulseUp() const { return mPulseUp; }

    //! address's A2DP went. True when it went with PulseAudio and is to be
    //! connected again.
    bool a2dpGone(const std::string &address, std::uint64_t now);
    //! address has A2DP again: nothing to do for it any more.
    void a2dpBack(const std::string &address);

    //! The headsets to connect now, as (address, adapter): none while
    //! PulseAudio is away (BlueZ would have no endpoint to configure).
    //! Each counts an attempt and is not handed out again unless failed()
    //! says so.
    std::vector<std::pair<std::string, std::string>> due();
    //! The attempt for address failed in a way worth another one. True
    //! when there is one left: due() hands it out again.
    bool failed(const std::string &address);
    //! The attempt for address is over (connected, already connected, or
    //! given up on).
    void done(const std::string &address);

    //! Whether address is still to be connected again (waiting, or its
    //! attempt under way).
    bool wanted(const std::string &address) const;
    //! Whether a headset still waits for due() to hand it out.
    bool pending() const;

private:
    struct Candidate
    {
        std::string adapter;
        int attempts = 0;
        bool inFlight = false;
    };

    bool mPulseUp = true;
    std::uint64_t mGoneAt = 0;
    //! Had A2DP when PulseAudio went; address -> adapter.
    std::map<std::string, std::string> mHadA2dp;
    //! To be connected again; address -> state.
    std::map<std::string, Candidate> mCandidates;
};

#endif // _A2DP_RESTORE_H
