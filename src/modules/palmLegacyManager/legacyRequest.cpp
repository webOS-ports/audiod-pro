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

#include "legacyRequest.h"

#include "messageUtils.h"

using namespace LegacyRequest;

const char *const LegacyRequest::SET_VOLUME =
    STRICT_SCHEMA(PROPS_3(PROP(volume, integer), PROP(scenario, string), SYSTEM_PARAMETERS)
                  REQUIRED_1(volume));

const char *const LegacyRequest::OFFSET_VOLUME =
    STRICT_SCHEMA(PROPS_4(PROP(offset, integer), PROP(scenario, string), PROP(unit, string),
                          SYSTEM_PARAMETERS)
                  REQUIRED_1(offset));

const char *const LegacyRequest::SET_MUTED =
    STRICT_SCHEMA(PROPS_2(PROP(muted, boolean), SYSTEM_PARAMETERS)
                  REQUIRED_1(muted));

const char *const LegacyRequest::SCENARIO =
    STRICT_SCHEMA(PROPS_2(PROP(scenario, string), SYSTEM_PARAMETERS)
                  REQUIRED_1(scenario));

const char *const LegacyRequest::CALL_STATUS =
    STRICT_SCHEMA(PROPS_2(PROP(lines, array), SYSTEM_PARAMETERS)
                  REQUIRED_1(lines));

/* Either id or name: dtmfKeys() refuses a payload with neither. */
const char *const LegacyRequest::PLAY_DTMF =
    STRICT_SCHEMA(PROPS_5(PROP(id, string), PROP(oneshot, boolean), PROP(feedbackOnly, boolean),
                          PROP(name, string), SYSTEM_PARAMETERS));

const char *const LegacyRequest::PLAY_FEEDBACK =
    STRICT_SCHEMA(PROPS_6(PROP(name, string), PROP(sink, string), PROP(play, boolean),
                          PROP(override, boolean), PROP(type, string), SYSTEM_PARAMETERS)
                  REQUIRED_1(name));

const char *const LegacyRequest::RINGER_SWITCH =
    STRICT_SCHEMA(PROPS_2(PROP(ringtone, boolean), SYSTEM_PARAMETERS)
                  REQUIRED_1(ringtone));

/* The results, not the targets, tell: asBool() writes false into its target
 * also for a missing key, asNumber() and asString() leave it as it was. */
std::optional<bool> LegacyRequest::boolean(const pbnjson::JValue &payload, const char *key)
{
    bool v = false;
    if (!payload.isObject() || payload[key].asBool(v) != CONV_OK)
        return std::nullopt;
    return v;
}

std::optional<int32_t> LegacyRequest::integer(const pbnjson::JValue &payload, const char *key)
{
    int32_t v = 0;
    if (!payload.isObject() || payload[key].asNumber<int32_t>(v) != CONV_OK)
        return std::nullopt;
    return v;
}

std::optional<std::string> LegacyRequest::string(const pbnjson::JValue &payload, const char *key)
{
    std::string v;
    if (!payload.isObject() || payload[key].asString(v) != CONV_OK)
        return std::nullopt;
    return v;
}

std::optional<std::string> LegacyRequest::dtmfKeys(const pbnjson::JValue &payload)
{
    std::optional<std::string> keys = string(payload, "id");
    if (!keys)
        keys = string(payload, "name");
    return keys;
}

bool LegacyRequest::validates(const char *payload, const char *schema, pbnjson::JValue *dom)
{
    if (!payload || !schema)
        return false;
    pbnjson::JSchemaFragment s(schema);
    pbnjson::JDomParser parser;
    if (!parser.parse(payload, s))
        return false;
    if (dom)
        *dom = parser.getDom();
    return true;
}
