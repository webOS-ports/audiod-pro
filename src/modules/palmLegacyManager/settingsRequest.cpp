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

#include "messageUtils.h"

/* REQUIRED() in SCHEMA_n() only declares a property: SCHEMA_1(REQUIRED(x,
 * boolean)) let '{}' through. The required list is REQUIRED_n(). */
const char *const SettingsRequest::SET_SCHEMA =
    STRICT_SCHEMA(PROPS_2(PROP(bluetoothHeadsetTakesCall, boolean), SYSTEM_PARAMETERS)
                  REQUIRED_1(bluetoothHeadsetTakesCall));

std::optional<bool> SettingsRequest::headsetTakesCall(const pbnjson::JValue &payload)
{
    /* asBool() writes false into its target before it looks at the value,
     * also for a key that is missing: its result is what tells. */
    bool takes = false;
    if (!payload.isObject() || payload[SETTING_HEADSET_TAKES_CALL].asBool(takes) != CONV_OK)
        return std::nullopt;
    return takes;
}

std::optional<bool> SettingsRequest::parseSet(const char *payload)
{
    if (!payload)
        return std::nullopt;
    pbnjson::JSchemaFragment schema(SET_SCHEMA);
    pbnjson::JDomParser parser;
    if (!parser.parse(payload, schema))
        return std::nullopt;
    return headsetTakesCall(parser.getDom());
}
