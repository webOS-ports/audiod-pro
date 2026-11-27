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

#ifndef _VOIP_ROUTE_H
#define _VOIP_ROUTE_H

#include <string>

#include "callState.h"
#include "classRoute.h"

/*
 * A VoIP call heard at the call volume alone, kept apart from PulseAudio so
 * it can be tested.
 *
 * module-palm-policy plays a virtual sink's streams on the sink the router
 * names for it, and for the phone's own outputs that is pcm_output: a
 * module-remap-sink over the card's sink (sink.primary_output on a Halium
 * phone), whose volume is the master volume. A VoIP call's stream there was
 * heard at the master volume times the call volume. The volume keys move
 * only the call volume during a call (CallVolume), so a master volume turned
 * down to quiet the ringtone kept the call quiet with the keys at the top
 * (Q25, WhatsApp, 2026-11-27: master 10, pcm_output at 25%, the earpiece's
 * call volume at 100 and every key press a no-op).
 *
 * A carrier call does not go through pcm_output at all: the modem plays it
 * at the HAL's voice volume, which the call volume sets. So for the length
 * of a VoIP call its classes go to the sink under pcm_output instead, which
 * plays on the same card and port (earpiece, loudspeaker, the jack, or the
 * headset's SCO link), and the call volume is all that turns it up or down.
 */
namespace VoipRoute
{
    //! The sink the master volume is on, which the call is taken off.
    extern const char *const SPEAKER;

    //! The redirect for mode, with speakerMaster the sink under SPEAKER
    //! (empty while not known): the VoIP classes off SPEAKER onto
    //! speakerMaster during a VoIP call, nothing otherwise.
    ClassRoute::Redirect callRedirect(ECallMode mode, const std::string &speakerMaster);
}

#endif // _VOIP_ROUTE_H
