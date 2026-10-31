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

#ifndef _BT_RING_H
#define _BT_RING_H

#include <string>
#include <vector>

#include "callState.h"
#include "classRoute.h"

/*
 * D1: with a Bluetooth headset carrying the audio, an incoming call rings
 * on the phone's loudspeaker, not in the headset. The headset rings by
 * itself: oFono's hands-free gateway sends it RING and +CLIP out of band and
 * never asks for in-band ringing, and the call moves to the headset (SCO)
 * only once it is answered.
 *
 * Legacy webOS did the same. Its Media_A2DP scenario sent only pmedia,
 * pflash, pnavigation and pdefaultapp to the A2DP sink and kept every other
 * sink, pringtones and palarm among them, on pcm_output (Veer audiod
 * 0x000367ec MediaScenarioModule, Media_A2DP block: configureRoute(sink, 0)
 * for sinks 0-3, 7 and 10-12, configureRoute(sink, 1) for 4-6 and 8); the
 * phone scenario became current only once a call was active. Sailfish never
 * routes a ringtone to a private accessory either.
 *
 * Here the router sends a display's whole range of stream classes to the
 * headset's sink; this is the override that keeps the ringtones class
 * there on the loudspeaker, kept apart from PulseAudio so it can be tested.
 * module-palm-policy routes palarm as its ringtones class, so the alarm
 * clock rings on the loudspeaker as well, as on legacy. The alerts and
 * notifications stay in the headset, as on Android: D1 is about the
 * ringtone. Bluetooth Settings' "Alerts on phone speaker" (stored as
 * sound/bluetoothAlertsOnSpeaker, off by default) keeps them on the
 * loudspeaker too, the way legacy did: the alerts class, which
 * module-palm-policy plays palerts, pnotifications and pcalendar in.
 *
 * The same override keeps a VoIP call's own sound off the headset's A2DP
 * sink. On a Halium phone a call reaches a hands-free headset through the
 * audio HAL (pcm_output with the droid card on its BT SCO port), never
 * through A2DP, so the voipcall classes stay on pcm_output for the whole
 * VoIP call, whatever its route: the earpiece, the loudspeaker, or the
 * headset over SCO. Without it the router sent them to A2DP with the rest:
 * on SCO the connector's stream broke every few seconds (43 "Broken pipe"
 * recoveries in 75 s, 2026-10-31 07:38:36-07:39:50), and with "Headset
 * takes calls" off, a headset connecting mid-call took the call over A2DP
 * while audiod and the Phone app said earpiece (D2-7, 07:40:09). Legacy's
 * Media_A2DP scenario never sent its phone sinks to A2DP either.
 */
class BtRing
{
public:
    //! The sink the ringtones play on instead of the headset.
    static const char *const SPEAKER;

    /* The stream classes (module-palm-policy EVirtualSink numbers) that
     * stay on pcm_output instead of the headset's A2DP sink. Outside a
     * call: the ringtones (with palarm), and with alertsOnSpeaker the
     * alerts as well. In a VoIP call: the call's own, voipcall and
     * voipcall1 (both in display 1's range); a ring or an alert then
     * belongs with the call. In a carrier call: none, its sound goes
     * through the modem, not a stream. */
    static std::vector<int> classes(ECallMode mode, bool alertsOnSpeaker = false);

    //! The output the audio goes to now; only a Bluetooth sink is kept.
    void outputIs(const std::string &sink);
    //! The Bluetooth sink carrying the audio, "" for none.
    const std::string &output() const { return mOutput; }

    //! Whether the alerts stay on the loudspeaker too (off: in the headset).
    void setAlertsOnSpeaker(bool on) { mAlertsOnSpeaker = on; }
    bool alertsOnSpeaker() const { return mAlertsOnSpeaker; }

    //! The Bluetooth sink that carried the audio last, kept after it went:
    //! "" until one has.
    const std::string &lastOutput() const { return mLast; }

    //! The router override for now: inactive in a carrier call, and
    //! outside a call without a Bluetooth output. In a VoIP call it holds
    //! for the headset that carried the audio last, gone or not: when that
    //! headset comes back mid-call the router sends the classes past it
    //! from the start, instead of to its A2DP sink until this module hears
    //! of the new output a moment later.
    ClassRoute::Override classOverride(ECallMode mode) const;

private:
    std::string mOutput;
    bool mAlertsOnSpeaker = false;
    std::string mLast;
};

#endif
