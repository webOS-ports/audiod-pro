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

#include "settingsRequest.h"

#include <cstdint>

#include "messageUtils.h"

/* REQUIRED() in SCHEMA_n() only declares a property: SCHEMA_1(REQUIRED(x,
 * boolean)) let '{}' through. The required list is REQUIRED_n(); either
 * setting will do, so one of two such lists. */
const char *const SettingsRequest::SET_SCHEMA =
    STRICT_SCHEMA(PROPS_3(PROP(bluetoothHeadsetTakesCall, boolean),
                          PROP(bluetoothAlertsOnSpeaker, boolean), SYSTEM_PARAMETERS)
                  ",\"anyOf\":[{\"type\":\"object\"" REQUIRED_1(bluetoothHeadsetTakesCall) "},"
                               "{\"type\":\"object\"" REQUIRED_1(bluetoothAlertsOnSpeaker) "}]");

static std::optional<bool> boolean(const pbnjson::JValue &payload, const char *key)
{
    /* asBool() writes false into its target before it looks at the value,
     * also for a key that is missing: its result is what tells. */
    bool value = false;
    if (!payload.isObject() || payload[key].asBool(value) != CONV_OK)
        return std::nullopt;
    return value;
}

std::optional<bool> SettingsRequest::headsetTakesCall(const pbnjson::JValue &payload)
{
    return boolean(payload, SETTING_HEADSET_TAKES_CALL);
}

std::optional<bool> SettingsRequest::alertsOnSpeaker(const pbnjson::JValue &payload)
{
    return boolean(payload, SETTING_ALERTS_ON_SPEAKER);
}

std::optional<SettingsRequest::Change> SettingsRequest::change(const pbnjson::JValue &payload)
{
    if (!payload.isObject())
        return std::nullopt;
    Change c{headsetTakesCall(payload), alertsOnSpeaker(payload)};
    if ((!c.headsetTakesCall && payload.hasKey(SETTING_HEADSET_TAKES_CALL)) ||
        (!c.alertsOnSpeaker && payload.hasKey(SETTING_ALERTS_ON_SPEAKER)) ||
        (!c.headsetTakesCall && !c.alertsOnSpeaker))
        return std::nullopt;
    return c;
}

std::optional<SettingsRequest::Change> SettingsRequest::parseSet(const char *payload)
{
    if (!payload)
        return std::nullopt;
    pbnjson::JSchemaFragment schema(SET_SCHEMA);
    pbnjson::JDomParser parser;
    if (!parser.parse(payload, schema))
        return std::nullopt;
    return change(parser.getDom());
}

std::optional<int> SettingsRequest::storedVolume(const pbnjson::JValue &value)
{
    /* Only a whole number: asNumber<int> would round 4.5 down and take a
     * string of digits. */
    if (!value.isNumber())
        return std::nullopt;
    int64_t whole = 0;
    if (value.asNumber(whole) != CONV_OK)
        return std::nullopt;
    if (whole < 0 || whole > 100)
        return std::nullopt;
    return static_cast<int>(whole);
}

std::optional<bool> SettingsRequest::storedFlag(const pbnjson::JValue &value)
{
    bool flag = false;
    if (!value.isBoolean() || value.asBool(flag) != CONV_OK)
        return std::nullopt;
    return flag;
}
