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

#include <cstring>

#include "pulseMessage.h"

namespace {

paudiodMsgHdr header()
{
    paudiodMsgHdr h{};
    h.msgType = 5;
    h.msgTmp = 0x01;
    h.msgVer = 1;
    h.msgLen = sizeof(paudiodMsgHdr);
    h.msgID = 42;
    return h;
}

paDeviceSet device()
{
    paDeviceSet d{};
    d.cardNo = 3;
    d.deviceNo = 7;
    d.isOutput = 1;
    std::strncpy(d.device, "pcm_output", DEVICE_NAME_LENGTH - 1);
    return d;
}

// module-palm-policy reads SIZE_MESG_TO_PULSE bytes for every message: all
// of them are there to be sent, whatever the message's struct is.
TEST(PulseMessage, EveryMessageIsAsLongAsTheModuleReads)
{
    EXPECT_EQ(packPulseMessage(header(), device()).size(), size_t(SIZE_MESG_TO_PULSE));
    EXPECT_EQ(packPulseMessage(header(), paRoutingSet{}).size(), size_t(SIZE_MESG_TO_PULSE));
    EXPECT_EQ(packPulseMessage(header(), paParamSet{}).size(), size_t(SIZE_MESG_TO_PULSE));
}

// The header first, the message's struct right after it.
TEST(PulseMessage, HeaderThenBody)
{
    const paudiodMsgHdr h = header();
    const paDeviceSet d = device();
    const std::vector<char> m = packPulseMessage(h, d);
    ASSERT_GE(m.size(), sizeof h + sizeof d);
    EXPECT_EQ(std::memcmp(m.data(), &h, sizeof h), 0);
    EXPECT_EQ(std::memcmp(m.data() + sizeof h, &d, sizeof d), 0);
}

// What follows the struct is zero, not whatever was in memory next to it.
TEST(PulseMessage, NothingButZeroAfterTheBody)
{
    const std::vector<char> m = packPulseMessage(header(), device());
    ASSERT_EQ(m.size(), size_t(SIZE_MESG_TO_PULSE));
    for (size_t i = sizeof(paudiodMsgHdr) + sizeof(paDeviceSet); i < m.size(); ++i)
        EXPECT_EQ(m[i], 0) << "byte " << i;
}

} // namespace
