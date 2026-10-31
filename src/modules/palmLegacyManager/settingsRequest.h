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

#ifndef _SETTINGS_REQUEST_H
#define _SETTINGS_REQUEST_H

#include <optional>

#include <pbnjson.hpp>

/*
 * What a setSettings request asks for (org.webosports.service.audio and
 * com.palm.audio). The keys are also what getSettings answers with and what
 * settingsservice keeps them under, as sound/bluetoothHeadsetTakesCall and
 * sound/bluetoothAlertsOnSpeaker.
 */
#define SETTING_HEADSET_TAKES_CALL "bluetoothHeadsetTakesCall"
#define SETTING_ALERTS_ON_SPEAKER "bluetoothAlertsOnSpeaker"
/* The ringtone volume and mute, com.palm.audio/ringtone's: the volume of the
 * pringtones stream, kept as sound/ringtoneVolume and sound/ringtoneMuted. */
#define SETTING_RINGTONE_VOLUME "ringtoneVolume"
#define SETTING_RINGTONE_MUTED "ringtoneMuted"

namespace SettingsRequest
{
    //! setSettings' schema: bluetoothHeadsetTakesCall and
    //! bluetoothAlertsOnSpeaker, booleans, at least one of them there;
    //! $activity is allowed, nothing else is.
    extern const char *const SET_SCHEMA;

    //! The settings a request changes; the ones it leaves are none.
    struct Change
    {
        std::optional<bool> headsetTakesCall;
        std::optional<bool> alertsOnSpeaker;

        bool operator==(const Change &o) const
        {
            return headsetTakesCall == o.headsetTakesCall && alertsOnSpeaker == o.alertsOnSpeaker;
        }
    };

    //! The value a payload asks bluetoothHeadsetTakesCall to be, or none
    //! if it holds no boolean bluetoothHeadsetTakesCall: never a default
    //! in its place.
    std::optional<bool> headsetTakesCall(const pbnjson::JValue &payload);
    //! The same for bluetoothAlertsOnSpeaker.
    std::optional<bool> alertsOnSpeaker(const pbnjson::JValue &payload);

    //! What a payload asks to change: none if it changes nothing, or holds
    //! one of the keys as anything but a boolean.
    std::optional<Change> change(const pbnjson::JValue &payload);

    //! A stored ringtone volume: an integer from 0 to 100, the range of
    //! com.palm.audio's setVolume and of Settings' "Ringtone Volume"
    //! slider; none for anything else, never a default in its place.
    std::optional<int> storedVolume(const pbnjson::JValue &value);
    //! A stored ringtone mute: a boolean, or none.
    std::optional<bool> storedFlag(const pbnjson::JValue &value);

    //! A payload checked against SET_SCHEMA the way LSMessageJsonParser
    //! checks it, then change(); none if either refuses it.
    std::optional<Change> parseSet(const char *payload);
}

#endif
