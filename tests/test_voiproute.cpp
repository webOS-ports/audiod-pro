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

#include <pulse/module-palm-policy.h>

#include "classRoute.h"
#include "voipRoute.h"

// A VoIP call plays on the sink under pcm_output, so the master volume
// (pcm_output's own) does not turn it down on top of the call volume.

namespace {

const std::string MASTER = "sink.primary_output";

TEST(VoipRoute, AVoipCallIsTakenOffTheMasterVolume)
{
    const ClassRoute::Redirect r = VoipRoute::callRedirect(eCallMode_Voip, MASTER);
    EXPECT_TRUE(r.active());
    EXPECT_EQ(r.from, "pcm_output");
    EXPECT_STREQ(VoipRoute::SPEAKER, "pcm_output");
    EXPECT_EQ(r.to, MASTER);
    EXPECT_EQ(r.classes, (std::vector<int>{evoipcall, evoipcall1}));
}

TEST(VoipRoute, OnlyTheCallsClassesMove)
{
    const auto runs = ClassRoute::redirect({{eVirtualSink_First, eVirtualSink_Last, "pcm_output"}},
                                           VoipRoute::callRedirect(eCallMode_Voip, MASTER));
    for (const ClassRoute::Range &run : runs)
    {
        for (int c = run.start; c <= run.end; c++)
        {
            const bool call = (c == evoipcall || c == evoipcall1);
            EXPECT_EQ(run.sink, call ? MASTER : std::string("pcm_output")) << "class " << c;
        }
    }
}

TEST(VoipRoute, NoCallOrACarrierCallStaysAsItIs)
{
    /* A carrier call is the modem's, at the HAL's voice volume. */
    EXPECT_FALSE(VoipRoute::callRedirect(eCallMode_None, MASTER).active());
    EXPECT_FALSE(VoipRoute::callRedirect(eCallMode_Carrier, MASTER).active());
}

TEST(VoipRoute, AnUnknownMasterSinkLeavesTheCallOnPcmOutput)
{
    EXPECT_FALSE(VoipRoute::callRedirect(eCallMode_Voip, "").active());
}

}
