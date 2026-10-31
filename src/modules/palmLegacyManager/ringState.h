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

#ifndef _RING_STATE_H
#define _RING_STATE_H

#include <cstdint>
#include <set>

#include "callState.h"

/*
 * Whether something is ringing: a ringtone or an alarm playing.
 *
 * A phone rings on the loudspeaker and the headphones together, as Android
 * and SailfishOS do, so a call or an alarm is heard with the headphones out
 * of the ears. Ringtones and alarms are the streams module-palm-policy tags
 * with media.type "pringtones" and "palarm" (the virtual sink they were
 * opened on). A corked stream is paused, not ringing.
 *
 * The alerts go there too, as legacy webOS did with a wired headset (see
 * usbRing.h): "palerts", "pnotifications" and "pcalendar", with the ringer
 * switch on. The jack and the loudspeaker are one port of the droid card,
 * so whatever else plays then is heard on the loudspeaker as well; an alert
 * therefore stays in the headphones while music plays ("pmedia",
 * "pdefaultapp"), or every notification would put the music on the
 * loudspeaker. A ringtone or an alarm does not: missing a call or an alarm
 * is worse.
 *
 * Kept apart from PulseAudio so it can be tested: the manager feeds it the
 * sink-input events and asks it what the jack should get.
 */
class RingState
{
public:
    static bool isRingType(const char *mediaType);
    static bool isAlertType(const char *mediaType);
    static bool isMediaType(const char *mediaType);

    //! A sink-input appeared or changed. True when ringing(), alerting() or
    //! mediaPlaying() changed.
    bool streamSeen(uint32_t index, const char *mediaType, bool corked);
    //! A sink-input went away. True when one of them changed.
    bool streamGone(uint32_t index);
    //! PulseAudio went away, and every stream index with it.
    void clear();

    bool ringing() const { return !mRinging.empty(); }
    bool alerting() const { return !mAlerting.empty(); }
    bool mediaPlaying() const { return !mMedia.empty(); }

    //! Whether the jack should get the loudspeaker too, call or not (see
    //! speakerToo()): a ringtone or an alarm; an alert with the ringer
    //! switch on and no music playing.
    bool speakerWanted(bool ringerOn) const;

    //! With the jack as the route: also the loudspeaker? Only outside a call:
    //! a ring during one is call waiting, which stays in the ear.
    static bool speakerToo(ECallMode mode, bool wanted);

private:
    //! ringing(), alerting() and mediaPlaying() as bits, to tell a change.
    unsigned snapshot() const
    {
        return (ringing() ? 1u : 0u) | (alerting() ? 2u : 0u) | (mediaPlaying() ? 4u : 0u);
    }

    std::set<uint32_t> mRinging;
    std::set<uint32_t> mAlerting;
    std::set<uint32_t> mMedia;
};

#endif
