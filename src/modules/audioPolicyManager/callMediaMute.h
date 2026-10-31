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

#ifndef _CALL_MEDIA_MUTE_H
#define _CALL_MEDIA_MUTE_H

#include <string>

/*
 * Which audio classes are silent while a call is up, kept apart from the
 * mixer so it can be tested.
 *
 * A call pauses the media sessions (palmLegacyManager::pauseMedia), but that
 * comes late or not at all: mediacontroller pauses a session that starts
 * during a call only after the app has reported it playing, and its pipeline
 * is audible by then (MK-2, 2026-10-31); and a player without a session
 * (librespot) is never paused, so it plays into the call. So while a call is
 * up audiod also mutes those classes in module-palm-policy, as legacy webOS
 * did (rampDownAndMute: pmedia, pflash, pnavigation, pdefaultapp), and as
 * Android does with its players of USAGE_MEDIA and USAGE_GAME (MediaFocusControl
 * USAGES_TO_MUTE_IN_RING_OR_CALL). When the call ends they are unmuted before
 * the paused session is told to play again.
 *
 * The call's mute is kept apart from the user's own: a stream gets the one or
 * the other (effective()), the user's stays what the status replies show, and
 * a class the user muted stays muted after the call, as on Android and
 * SailfishOS (a volume factor of their own) and unlike legacy webOS (which
 * cleared it).
 *
 * Muted, by module-palm-policy class (tables-default.h), with the phone-era
 * sinks it routes as one of them (virtualsinkaliases):
 *   pmedia (and pvvm)        media players
 *   pdefaultapp (and pflash) every stream that names no sink: the Music app,
 *                            librespot, any Qt or GStreamer player
 *   default1, default2       a web app's <audio>/<video> (g-media-pipeline
 *                            plays to the display's default sink)
 *   btstream                 A2DP sink audio played back here
 *   fm, am, hdradio, radio   broadcast radio
 * Never muted:
 *   pringtones (palarm)      the ring of a waiting call, an alarm
 *   palerts (pnotifications, pcalendar), pfeedback (pDTMF): alerts, keypad
 *   voipcall, voipcall1, voipcall2 (pvoip), btcall: the call's own downlink.
 *                            Connector VoIP calls play to ALSA "voip" =
 *                            voipcall; Chromium's streams on a real sink are
 *                            tagged voipcall; media.role phone goes to btcall
 *   ptts, tts1, tts2 (pnavigation): speech - a screen reader the user needs
 *                            to work the phone during the call, and spoken
 *                            directions, which Android and SailfishOS let
 *                            through (legacy webOS muted pnavigation). Nothing
 *                            on LuneOS or legacy webOS reads caller ID aloud.
 *   pvoicerecognition (pvoicedial): the voice dialer's and assistant's prompts
 *   peffects                 nothing on LuneOS plays there; legacy webOS left
 *                            its effects alone too
 */
namespace CallMediaMute
{
    //! Whether streams of this type are muted while a call is up: an audiod
    //! stream type, or the media.type module-palm-policy tags a sink-input
    //! with (the virtual sink it was opened on, phone-era names included).
    bool mutedInCall(const std::string &type);
    bool mutedInCall(const char *type);

    //! The mute a stream of type gets: the user's, or the call's.
    bool effective(const std::string &type, bool userMuted, bool callUp);

    //! Whether a sink-input seen while callUp, of mediaType and muted or
    //! not, is to be muted now. Never an unmute: only module-palm-policy
    //! unmutes, with the user's mute, when the call ends.
    bool muteNow(bool callUp, const char *mediaType, bool muted);
}

#endif // _CALL_MEDIA_MUTE_H
