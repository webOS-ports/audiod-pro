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

#include "callMediaMute.h"

using CallMediaMute::effective;
using CallMediaMute::mutedInCall;
using CallMediaMute::muteNow;

TEST(CallMediaMute, MediaClassesAreMuted)
{
    for (const char *t : {"pmedia", "pdefaultapp", "default1", "default2", "btstream",
                          "fm", "am", "hdradio", "radio"})
        EXPECT_TRUE(mutedInCall(t)) << t;
}

TEST(CallMediaMute, PhoneEraMediaSinksAreMuted)
{
    // module-palm-policy routes them as pmedia and pdefaultapp
    EXPECT_TRUE(mutedInCall("pvvm"));
    EXPECT_TRUE(mutedInCall("pflash"));
}

TEST(CallMediaMute, RingsAlertsAndFeedbackPlayOn)
{
    for (const char *t : {"pringtones", "palarm", "palerts", "pnotifications", "pcalendar",
                          "pfeedback", "pDTMF"})
        EXPECT_FALSE(mutedInCall(t)) << t;
}

TEST(CallMediaMute, TheCallsOwnAudioPlaysOn)
{
    for (const char *t : {"voipcall", "voipcall1", "voipcall2", "pvoip", "btcall"})
        EXPECT_FALSE(mutedInCall(t)) << t;
}

TEST(CallMediaMute, SpeechAndPromptsPlayOn)
{
    for (const char *t : {"ptts", "tts1", "tts2", "pnavigation", "pvoicerecognition",
                          "pvoicedial", "peffects"})
        EXPECT_FALSE(mutedInCall(t)) << t;
}

TEST(CallMediaMute, UnknownOrMissingTypesAreLeftAlone)
{
    EXPECT_FALSE(mutedInCall(static_cast<const char *>(nullptr)));
    EXPECT_FALSE(mutedInCall(""));
    EXPECT_FALSE(mutedInCall("pmedia2"));
    EXPECT_FALSE(mutedInCall("PMEDIA"));
    EXPECT_FALSE(mutedInCall("sink.primary_output"));
    EXPECT_FALSE(mutedInCall(std::string("pmedi")));
    EXPECT_TRUE(mutedInCall(std::string("pmedia")));
}

TEST(CallMediaMute, NoCallLeavesTheUsersMute)
{
    EXPECT_FALSE(effective("pmedia", false, false));
    EXPECT_TRUE(effective("pmedia", true, false));
}

TEST(CallMediaMute, ACallMutesMedia)
{
    EXPECT_TRUE(effective("pmedia", false, true));
    EXPECT_TRUE(effective("pdefaultapp", false, true));
}

TEST(CallMediaMute, AUserMutedClassStaysMutedAfterTheCall)
{
    EXPECT_TRUE(effective("pdefaultapp", true, true));
    EXPECT_TRUE(effective("pdefaultapp", true, false));
}

TEST(CallMediaMute, ACallDoesNotMuteTheRingtone)
{
    EXPECT_FALSE(effective("pringtones", false, true));
    EXPECT_TRUE(effective("pringtones", true, true));
}

TEST(CallMediaMute, AStreamAppearingDuringACallIsMuted)
{
    EXPECT_TRUE(muteNow(true, "pdefaultapp", false));
    EXPECT_TRUE(muteNow(true, "default1", false));
}

TEST(CallMediaMute, OnlyOnceAndOnlyDuringACall)
{
    EXPECT_FALSE(muteNow(true, "pdefaultapp", true));
    EXPECT_FALSE(muteNow(false, "pdefaultapp", false));
    EXPECT_FALSE(muteNow(true, "voipcall", false));
    EXPECT_FALSE(muteNow(true, nullptr, false));
}
