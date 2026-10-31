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

#ifndef _LEGACY_REQUEST_H
#define _LEGACY_REQUEST_H

#include <cstdint>
#include <optional>
#include <string>

#include <pbnjson.hpp>

/*
 * The payloads the com.palm.audio / org.webosports.service.audio methods
 * take, and reading them. SCHEMA_n(REQUIRED(x, type)) only declares x: it
 * never made x required, so '{}' got through and the handler went on with
 * its own default, or with the false pbnjson's asBool() writes into its
 * target for a missing key. Each schema here lists what must be there with
 * REQUIRED_n, keeps every key a caller sends, and still allows $activity as
 * SCHEMA_n did; the readers give no value instead of a default.
 */
namespace LegacyRequest
{
    //! setVolume (root and per category): volume, an integer; scenario.
    extern const char *const SET_VOLUME;
    //! offsetVolume: offset, an integer; scenario, unit.
    extern const char *const OFFSET_VOLUME;
    //! setMuted per category and phone/setMuted: muted, a boolean.
    extern const char *const SET_MUTED;
    //! setCurrentScenario, enableScenario, disableScenario: scenario.
    extern const char *const SCENARIO;
    //! phone/CallStatusUpdate: lines, an array ({"lines":[]} ends the calls).
    extern const char *const CALL_STATUS;
    //! dtmf/playDTMF: id (enyo's dialpad) or name (the QML Phone app);
    //! oneshot, feedbackOnly.
    extern const char *const PLAY_DTMF;
    //! playFeedback: name; sink, play, override, type.
    extern const char *const PLAY_FEEDBACK;
    //! state/setRingerSwitch: ringtone, a boolean.
    extern const char *const RINGER_SWITCH;

    //! payload[key] if it is there and of that type; none otherwise.
    std::optional<bool> boolean(const pbnjson::JValue &payload, const char *key);
    std::optional<int32_t> integer(const pbnjson::JValue &payload, const char *key);
    std::optional<std::string> string(const pbnjson::JValue &payload, const char *key);

    //! The keys playDTMF is to play: id, else name; none without either.
    std::optional<std::string> dtmfKeys(const pbnjson::JValue &payload);

    //! Whether payload passes schema the way LSMessageJsonParser checks
    //! it; the parsed payload goes to *dom if given.
    bool validates(const char *payload, const char *schema, pbnjson::JValue *dom = nullptr);
}

#endif
