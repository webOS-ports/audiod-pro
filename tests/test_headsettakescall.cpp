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

#include "btCall.h"
#include "storedSetting.h"

// Bluetooth Settings' "Headset takes calls" (D2): with it off, a hands-free
// headset that connects during a call does not take the call.

namespace {

typedef BtCall::Choice Choice;
typedef BtCall::Action Action;
typedef BtCall::Arrival Arrival;
typedef BtCall::Sco Sco;

const BtCall::Situation idle{eCallMode_None, Choice::Default, false};
const BtCall::Situation call{eCallMode_Carrier, Choice::Default, false};
const BtCall::Situation callJack{eCallMode_Carrier, Choice::Default, true};
const BtCall::Situation callBt{eCallMode_Carrier, Choice::Bluetooth, false};

TEST(HeadsetTakesCall, OnByDefault)
{
    BtCall bt;
    EXPECT_TRUE(bt.takesCallOnConnect());
    EXPECT_EQ(bt.sync(call), Action::None);     // the call starts, no headset
    bt.cardAdded("/card_1", "A");
    EXPECT_TRUE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::Connect);
}

TEST(HeadsetTakesCall, OffAHeadsetConnectingMidCallLeavesTheCall)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    EXPECT_EQ(bt.sync(call), Action::None);
    EXPECT_TRUE(bt.cardAdded("/card_1", "A"));
    // connected and shown, but the call stays on the earpiece
    EXPECT_TRUE(bt.available());
    EXPECT_EQ(bt.address(), "A");
    EXPECT_FALSE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::None);
    EXPECT_EQ(bt.sco(), Sco::Down);
}

TEST(HeadsetTakesCall, OffTheRouteButtonStillPicksIt)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.sync(call);
    bt.cardAdded("/card_1", "A");
    bt.chose(Choice::Bluetooth);
    EXPECT_TRUE(bt.wanted(callBt));
    ASSERT_EQ(bt.sync(callBt), Action::Connect);
    EXPECT_EQ(bt.scoCard(), "/card_1");
}

TEST(HeadsetTakesCall, OffTheHeadsetsButtonStillTakesIt)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.sync(call);
    bt.cardAdded("/card_1", "A");
    EXPECT_EQ(bt.scoConnected("/card_1", call), Arrival::ByHeadset);
    EXPECT_TRUE(bt.routed(call));
    EXPECT_EQ(bt.sync(call), Action::None);     // kept, not closed again
}

TEST(HeadsetTakesCall, OffAHeadsetConnectedBeforeTheCallTakesIt)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.sync(idle);
    bt.cardAdded("/card_1", "A");
    EXPECT_TRUE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::Connect);
}

TEST(HeadsetTakesCall, OffTheNextCallUsesTheHeadset)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.sync(call);
    bt.cardAdded("/card_1", "A");
    EXPECT_FALSE(bt.wanted(call));
    bt.callEnded();
    bt.sync(idle);
    EXPECT_TRUE(bt.wanted(call));
}

TEST(HeadsetTakesCall, OffASecondHeadsetDoesNotTakeTheCallFromTheFirst)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.cardAdded("/card_1", "A");
    ASSERT_EQ(bt.sync(call), Action::Connect);
    ASSERT_EQ(bt.scoConnected("/card_1", call), Arrival::Asked);
    bt.cardAdded("/card_2", "B");
    EXPECT_EQ(bt.sync(call), Action::None);     // the link to A stays
    EXPECT_EQ(bt.scoCard(), "/card_1");
    EXPECT_TRUE(bt.routed(call));
}

TEST(HeadsetTakesCall, OffNothingTheUserChoseIsUndone)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.cardAdded("/card_1", "A");
    bt.sync(call);
    bt.chose(Choice::Handset);                  // the user moved it to the ear
    EXPECT_FALSE(bt.wanted(call));
    bt.cardAdded("/card_2", "B");               // on: this would undo the choice
    EXPECT_FALSE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::None);
}

TEST(HeadsetTakesCall, OffTheJackKeepsTheCall)
{
    BtCall bt;
    bt.setTakesCallOnConnect(false);
    bt.sync(callJack);
    bt.jackPlugged();
    bt.cardAdded("/card_1", "A");
    EXPECT_FALSE(bt.wanted(callJack));
}

TEST(StoredSetting, StartsAtTheDefaultAndTakesTheStoredValue)
{
    StoredSetting<bool> s(true);
    EXPECT_TRUE(s.value());
    EXPECT_FALSE(s.stored(true));
    EXPECT_TRUE(s.stored(false));
    EXPECT_FALSE(s.value());
}

TEST(StoredSetting, WhatTheUserSetWinsOverALateStoredValue)
{
    StoredSetting<bool> s(true);
    EXPECT_TRUE(s.set(false));
    EXPECT_FALSE(s.stored(true));
    EXPECT_FALSE(s.value());
    EXPECT_FALSE(s.set(false));
    EXPECT_TRUE(s.set(true));
    EXPECT_TRUE(s.value());
}

}
