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

#include <gtest/gtest.h>

#include "settingsRequest.h"

// setSettings' payload (D2, Bluetooth Settings' "Headset takes calls"): only
// a boolean bluetoothHeadsetTakesCall changes the setting. '{}' used to pass
// the schema and turn it off.

namespace {

bool schemaTakes(const char *payload)
{
    pbnjson::JSchemaFragment schema(SettingsRequest::SET_SCHEMA);
    pbnjson::JDomParser parser;
    return parser.parse(payload, schema);
}

TEST(SettingsRequest, AnEmptyPayloadIsRefused)
{
    EXPECT_FALSE(schemaTakes("{}"));
    EXPECT_EQ(SettingsRequest::parseSet("{}"), std::nullopt);
}

TEST(SettingsRequest, TrueAndFalseAreTaken)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":true}"), std::optional<bool>(true));
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":false}"), std::optional<bool>(false));
}

TEST(SettingsRequest, ANonBooleanIsRefused)
{
    for (const char *p : {"{\"bluetoothHeadsetTakesCall\":\"no\"}",
                          "{\"bluetoothHeadsetTakesCall\":0}",
                          "{\"bluetoothHeadsetTakesCall\":null}",
                          "{\"bluetoothHeadsetTakesCall\":{}}"})
    {
        EXPECT_FALSE(schemaTakes(p)) << p;
        EXPECT_EQ(SettingsRequest::parseSet(p), std::nullopt) << p;
    }
}

TEST(SettingsRequest, AnythingElseIsRefused)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":true,\"other\":1}"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("{\"subscribe\":true}"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("[]"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("not json"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet(""), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet(nullptr), std::nullopt);
}

TEST(SettingsRequest, AnActivityIsAllowedWithTheSetting)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":false,\"$activity\":{}}"),
              std::optional<bool>(false));
    EXPECT_EQ(SettingsRequest::parseSet("{\"$activity\":{}}"), std::nullopt);
}

// What the handler does with a payload that got past the schema: no
// default for a missing or non-boolean key (pbnjson's asBool() writes
// false into its target for a missing key).
TEST(SettingsRequest, NoDefaultForAMissingKey)
{
    EXPECT_EQ(SettingsRequest::headsetTakesCall(pbnjson::Object()), std::nullopt);
    pbnjson::JValue s = pbnjson::Object();
    s.put(SETTING_HEADSET_TAKES_CALL, "no");
    EXPECT_EQ(SettingsRequest::headsetTakesCall(s), std::nullopt);
    EXPECT_EQ(SettingsRequest::headsetTakesCall(pbnjson::Array()), std::nullopt);
    pbnjson::JValue t = pbnjson::Object();
    t.put(SETTING_HEADSET_TAKES_CALL, true);
    EXPECT_EQ(SettingsRequest::headsetTakesCall(t), std::optional<bool>(true));
}

} // namespace
