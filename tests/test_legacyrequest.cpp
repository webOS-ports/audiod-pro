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

#include "legacyRequest.h"

// The com.palm.audio / org.webosports.service.audio payloads: '{}' or a
// missing key is refused, what the callers in LuneOS really send is taken.

namespace {

using namespace LegacyRequest;

bool ok(const char *payload, const char *schema) { return validates(payload, schema); }

pbnjson::JValue dom(const char *payload)
{
    return pbnjson::JDomParser::fromString(payload);
}

TEST(LegacyRequest, SetVolumeNeedsAnIntegerVolume)
{
    EXPECT_FALSE(ok("{}", SET_VOLUME));
    EXPECT_FALSE(ok("{\"scenario\":\"media_back_speaker\"}", SET_VOLUME));
    EXPECT_FALSE(ok("{\"volume\":\"50\"}", SET_VOLUME));
    EXPECT_FALSE(ok("{\"volume\":null}", SET_VOLUME));
    EXPECT_FALSE(ok("{\"volume\":50,\"soundOutput\":\"speaker\"}", SET_VOLUME));
    // luna-next-cardshell's device menu, Settings' Sounds page
    EXPECT_TRUE(ok("{\"volume\":50}", SET_VOLUME));
    EXPECT_TRUE(ok("{\"volume\":0,\"scenario\":\"ringtone_default\"}", SET_VOLUME));
    EXPECT_TRUE(ok("{\"volume\":50,\"$activity\":{}}", SET_VOLUME));
}

TEST(LegacyRequest, OffsetVolumeNeedsAnIntegerOffset)
{
    EXPECT_FALSE(ok("{}", OFFSET_VOLUME));
    EXPECT_FALSE(ok("{\"unit\":\"percent\"}", OFFSET_VOLUME));
    EXPECT_TRUE(ok("{\"offset\":-10}", OFFSET_VOLUME));
    EXPECT_TRUE(ok("{\"offset\":10,\"scenario\":\"media_headset\",\"unit\":\"percent\"}", OFFSET_VOLUME));
}

TEST(LegacyRequest, SetMutedNeedsABooleanMuted)
{
    EXPECT_FALSE(ok("{}", SET_MUTED));
    EXPECT_FALSE(ok("{\"muted\":\"true\"}", SET_MUTED));
    EXPECT_FALSE(ok("{\"muted\":1}", SET_MUTED));
    // Settings' ringer switch: ringtone/setMuted
    EXPECT_TRUE(ok("{\"muted\":true}", SET_MUTED));
    EXPECT_TRUE(ok("{\"muted\":false}", SET_MUTED));
}

TEST(LegacyRequest, RingerSwitchNeedsABooleanRingtone)
{
    EXPECT_FALSE(ok("{}", RINGER_SWITCH));
    EXPECT_FALSE(ok("{\"ringtone\":0}", RINGER_SWITCH));
    EXPECT_TRUE(ok("{\"ringtone\":false}", RINGER_SWITCH));
    EXPECT_TRUE(ok("{\"ringtone\":true}", RINGER_SWITCH));
}

TEST(LegacyRequest, ScenarioNeedsAScenario)
{
    EXPECT_FALSE(ok("{}", SCENARIO));
    EXPECT_FALSE(ok("{\"scenario\":7}", SCENARIO));
    // the messaging connectors' VoIP calls
    EXPECT_TRUE(ok("{\"scenario\":\"phone_back_speaker\"}", SCENARIO));
}

TEST(LegacyRequest, CallStatusUpdateNeedsLines)
{
    EXPECT_FALSE(ok("{}", CALL_STATUS));
    EXPECT_FALSE(ok("{\"lines\":{}}", CALL_STATUS));
    EXPECT_TRUE(ok("{\"lines\":[]}", CALL_STATUS));
    // WhatsApp's connector, extra keys inside the call are the call's own
    EXPECT_TRUE(ok("{\"lines\":[{\"state\":\"active\",\"calls\":[{\"id\":\"1\",\"address\":\"whatsapp\","
                   "\"origin\":\"outgoing\",\"video\":false,\"transport\":\"com.palm.whatsapp\"}]}]}",
                   CALL_STATUS));
}

TEST(LegacyRequest, PlayDtmfTakesIdOrName)
{
    // the QML Phone app: name, feedbackOnly in a call
    pbnjson::JValue d;
    ASSERT_TRUE(validates("{\"name\":\"5\",\"feedbackOnly\":true}", PLAY_DTMF, &d));
    EXPECT_EQ(dtmfKeys(d), std::optional<std::string>("5"));
    // enyo's dialpad: id, oneshot, feedbackOnly
    ASSERT_TRUE(validates("{\"id\":\"#\",\"oneshot\":false,\"feedbackOnly\":true}", PLAY_DTMF, &d));
    EXPECT_EQ(dtmfKeys(d), std::optional<std::string>("#"));
    ASSERT_TRUE(validates("{\"id\":\"1\",\"name\":\"2\"}", PLAY_DTMF, &d));
    EXPECT_EQ(dtmfKeys(d), std::optional<std::string>("1"));
    // neither: through the schema, refused by dtmfKeys()
    ASSERT_TRUE(validates("{}", PLAY_DTMF, &d));
    EXPECT_EQ(dtmfKeys(d), std::nullopt);
    ASSERT_TRUE(validates("{\"oneshot\":true}", PLAY_DTMF, &d));
    EXPECT_EQ(dtmfKeys(d), std::nullopt);
    EXPECT_FALSE(ok("{\"id\":5}", PLAY_DTMF));
}

TEST(LegacyRequest, PlayFeedbackNeedsAName)
{
    EXPECT_FALSE(ok("{}", PLAY_FEEDBACK));
    EXPECT_FALSE(ok("{\"sink\":\"pfeedback\"}", PLAY_FEEDBACK));
    // the camera's shutter, Settings, the shell's FeedbackSound
    EXPECT_TRUE(ok("{\"name\":\"shutter\"}", PLAY_FEEDBACK));
    // LunaSysMgr's SoundPlayerPool: name and sink
    EXPECT_TRUE(ok("{\"name\":\"key\",\"sink\":\"pfeedback\"}", PLAY_FEEDBACK));
    EXPECT_TRUE(ok("{\"name\":\"key\",\"play\":false,\"override\":true,\"type\":\"x\"}", PLAY_FEEDBACK));
}

TEST(LegacyRequest, ReadersGiveNoDefault)
{
    const pbnjson::JValue empty = dom("{}");
    EXPECT_EQ(boolean(empty, "muted"), std::nullopt);
    EXPECT_EQ(integer(empty, "volume"), std::nullopt);
    EXPECT_EQ(string(empty, "scenario"), std::nullopt);

    const pbnjson::JValue wrong = dom("{\"muted\":\"no\",\"volume\":\"7\",\"scenario\":3}");
    EXPECT_EQ(boolean(wrong, "muted"), std::nullopt);
    EXPECT_EQ(integer(wrong, "volume"), std::nullopt);
    EXPECT_EQ(string(wrong, "scenario"), std::nullopt);

    EXPECT_EQ(boolean(pbnjson::Array(), "muted"), std::nullopt);

    const pbnjson::JValue good = dom("{\"muted\":false,\"volume\":0,\"scenario\":\"\"}");
    EXPECT_EQ(boolean(good, "muted"), std::optional<bool>(false));
    EXPECT_EQ(integer(good, "volume"), std::optional<int32_t>(0));
    EXPECT_EQ(string(good, "scenario"), std::optional<std::string>(""));
}

TEST(LegacyRequest, AVolumeOutOfRangeOfAnIntIsNoVolume)
{
    EXPECT_EQ(integer(dom("{\"volume\":99999999999}"), "volume"), std::nullopt);
}

} // namespace
