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

#include <optional>
#include <string>

#include "settingsRequest.h"

// setSettings' payload (Bluetooth Settings' "Headset takes calls", D2, and
// "Alerts on phone speaker"): only a boolean bluetoothHeadsetTakesCall or
// bluetoothAlertsOnSpeaker, or both, changes a setting. '{}' used to pass the
// schema and turn "Headset takes calls" off.

namespace {

using Change = SettingsRequest::Change;

bool schemaTakes(const char *payload)
{
    pbnjson::JSchemaFragment schema(SettingsRequest::SET_SCHEMA);
    pbnjson::JDomParser parser;
    return parser.parse(payload, schema);
}

std::optional<Change> takes(bool v) { return Change{v, std::nullopt}; }
std::optional<Change> alerts(bool v) { return Change{std::nullopt, v}; }

TEST(SettingsRequest, AnEmptyPayloadIsRefused)
{
    EXPECT_FALSE(schemaTakes("{}"));
    EXPECT_EQ(SettingsRequest::parseSet("{}"), std::nullopt);
}

TEST(SettingsRequest, TrueAndFalseAreTaken)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":true}"), takes(true));
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":false}"), takes(false));
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothAlertsOnSpeaker\":true}"), alerts(true));
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothAlertsOnSpeaker\":false}"), alerts(false));
}

TEST(SettingsRequest, BothCanBeSetAtOnce)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":false,\"bluetoothAlertsOnSpeaker\":true}"),
              std::optional<Change>(Change{false, true}));
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothAlertsOnSpeaker\":false,\"bluetoothHeadsetTakesCall\":true}"),
              std::optional<Change>(Change{true, false}));
}

TEST(SettingsRequest, ANonBooleanIsRefused)
{
    for (const char *key : {"bluetoothHeadsetTakesCall", "bluetoothAlertsOnSpeaker"})
    {
        for (const char *v : {"\"no\"", "0", "null", "{}", "[]"})
        {
            const std::string alone = std::string("{\"") + key + "\":" + v + "}";
            EXPECT_FALSE(schemaTakes(alone.c_str())) << alone;
            EXPECT_EQ(SettingsRequest::parseSet(alone.c_str()), std::nullopt) << alone;
        }
    }
    // also beside the other, given right
    for (const char *p : {"{\"bluetoothHeadsetTakesCall\":true,\"bluetoothAlertsOnSpeaker\":\"yes\"}",
                          "{\"bluetoothHeadsetTakesCall\":1,\"bluetoothAlertsOnSpeaker\":true}"})
    {
        EXPECT_FALSE(schemaTakes(p)) << p;
        EXPECT_EQ(SettingsRequest::parseSet(p), std::nullopt) << p;
    }
}

TEST(SettingsRequest, AnythingElseIsRefused)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":true,\"other\":1}"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothAlertsOnSpeaker\":true,\"other\":1}"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("{\"subscribe\":true}"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("[]"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet("not json"), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet(""), std::nullopt);
    EXPECT_EQ(SettingsRequest::parseSet(nullptr), std::nullopt);
}

TEST(SettingsRequest, AnActivityIsAllowedWithASetting)
{
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothHeadsetTakesCall\":false,\"$activity\":{}}"),
              takes(false));
    EXPECT_EQ(SettingsRequest::parseSet("{\"bluetoothAlertsOnSpeaker\":true,\"$activity\":{}}"),
              alerts(true));
    EXPECT_FALSE(schemaTakes("{\"$activity\":{}}"));
    EXPECT_EQ(SettingsRequest::parseSet("{\"$activity\":{}}"), std::nullopt);
}

// What the handler does with a payload that got past the schema: no
// default for a missing or non-boolean key (pbnjson's asBool() writes
// false into its target for a missing key).
TEST(SettingsRequest, NoDefaultForAMissingKey)
{
    EXPECT_EQ(SettingsRequest::headsetTakesCall(pbnjson::Object()), std::nullopt);
    EXPECT_EQ(SettingsRequest::alertsOnSpeaker(pbnjson::Object()), std::nullopt);
    pbnjson::JValue s = pbnjson::Object();
    s.put(SETTING_HEADSET_TAKES_CALL, "no");
    EXPECT_EQ(SettingsRequest::headsetTakesCall(s), std::nullopt);
    EXPECT_EQ(SettingsRequest::headsetTakesCall(pbnjson::Array()), std::nullopt);
    EXPECT_EQ(SettingsRequest::alertsOnSpeaker(pbnjson::Array()), std::nullopt);
    pbnjson::JValue t = pbnjson::Object();
    t.put(SETTING_HEADSET_TAKES_CALL, true);
    EXPECT_EQ(SettingsRequest::headsetTakesCall(t), std::optional<bool>(true));
    EXPECT_EQ(SettingsRequest::alertsOnSpeaker(t), std::nullopt);
    pbnjson::JValue a = pbnjson::Object();
    a.put(SETTING_ALERTS_ON_SPEAKER, false);
    EXPECT_EQ(SettingsRequest::alertsOnSpeaker(a), std::optional<bool>(false));
    EXPECT_EQ(SettingsRequest::headsetTakesCall(a), std::nullopt);
}

TEST(SettingsRequest, AChangeNeedsASettingAndNoBadOne)
{
    EXPECT_EQ(SettingsRequest::change(pbnjson::Object()), std::nullopt);
    EXPECT_EQ(SettingsRequest::change(pbnjson::Array()), std::nullopt);
    pbnjson::JValue bad = pbnjson::Object();
    bad.put(SETTING_ALERTS_ON_SPEAKER, true);
    bad.put(SETTING_HEADSET_TAKES_CALL, "no");
    EXPECT_EQ(SettingsRequest::change(bad), std::nullopt);
    pbnjson::JValue bad2 = pbnjson::Object();
    bad2.put(SETTING_HEADSET_TAKES_CALL, false);
    bad2.put(SETTING_ALERTS_ON_SPEAKER, 1);
    EXPECT_EQ(SettingsRequest::change(bad2), std::nullopt);
    pbnjson::JValue good = pbnjson::Object();
    good.put(SETTING_ALERTS_ON_SPEAKER, true);
    EXPECT_EQ(SettingsRequest::change(good), alerts(true));
    EXPECT_STREQ(SETTING_ALERTS_ON_SPEAKER, "bluetoothAlertsOnSpeaker");
}

} // namespace
