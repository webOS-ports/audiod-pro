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
 * com.palm.audio). The key is also what getSettings answers with and what
 * settingsservice keeps it under, as sound/bluetoothHeadsetTakesCall.
 */
#define SETTING_HEADSET_TAKES_CALL "bluetoothHeadsetTakesCall"

namespace SettingsRequest
{
    //! setSettings' schema: bluetoothHeadsetTakesCall, a boolean, which
    //! must be there; $activity is allowed, nothing else is.
    extern const char *const SET_SCHEMA;

    //! The value a payload asks bluetoothHeadsetTakesCall to be, or none
    //! if it holds no boolean bluetoothHeadsetTakesCall: never a default
    //! in its place.
    std::optional<bool> headsetTakesCall(const pbnjson::JValue &payload);

    //! A payload checked against SET_SCHEMA the way LSMessageJsonParser
    //! checks it, then headsetTakesCall(); none if either refuses it.
    std::optional<bool> parseSet(const char *payload);
}

#endif
