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

namespace {

typedef BtCall::Choice Choice;
typedef BtCall::Action Action;
typedef BtCall::Arrival Arrival;
typedef BtCall::Sco Sco;

const BtCall::Situation idle{eCallMode_None, Choice::Default, false};
const BtCall::Situation call{eCallMode_Carrier, Choice::Default, false};
const BtCall::Situation voip{eCallMode_Voip, Choice::Default, false};
const BtCall::Situation callJack{eCallMode_Carrier, Choice::Default, true};
const BtCall::Situation callSpeaker{eCallMode_Carrier, Choice::Speaker, false};
const BtCall::Situation callBt{eCallMode_Carrier, Choice::Bluetooth, false};
const BtCall::Situation callBtJack{eCallMode_Carrier, Choice::Bluetooth, true};
const BtCall::Situation callWired{eCallMode_Carrier, Choice::Handset, true};

// Opens the link sync() asks for, the way oFono answers.
void bringUp(BtCall &bt, const BtCall::Situation &s)
{
    ASSERT_EQ(bt.sync(s), Action::Connect);
    ASSERT_EQ(bt.sco(), Sco::Connecting);
    ASSERT_EQ(bt.scoConnected(bt.scoCard(), s), Arrival::Asked);
    ASSERT_EQ(bt.sco(), Sco::Up);
}

TEST(BtCall, NothingWithoutAHeadset)
{
    BtCall bt;
    EXPECT_FALSE(bt.available());
    EXPECT_EQ(bt.card(), "");
    EXPECT_EQ(bt.address(), "");
    EXPECT_FALSE(bt.wanted(call));
    EXPECT_FALSE(bt.wanted(callBt));     // chosen, but nothing to go to
    EXPECT_EQ(bt.sync(callBt), Action::None);
}

TEST(BtCall, CardsComeAndGo)
{
    BtCall bt;
    EXPECT_TRUE(bt.cardAdded("/card_1", "00:11:22:33:44:55"));
    EXPECT_FALSE(bt.cardAdded("/card_1", "00:11:22:33:44:55"));   // reported twice
    EXPECT_FALSE(bt.cardAdded("", "00:11:22:33:44:66"));
    EXPECT_TRUE(bt.available());
    EXPECT_EQ(bt.card(), "/card_1");
    EXPECT_EQ(bt.address(), "00:11:22:33:44:55");
    EXPECT_FALSE(bt.cardRemoved("/card_9"));
    EXPECT_TRUE(bt.cardRemoved("/card_1"));
    EXPECT_FALSE(bt.cardRemoved("/card_1"));
    EXPECT_FALSE(bt.available());
}

TEST(BtCall, TheHeadsetConnectedLastIsUsed)
{
    BtCall bt;
    bt.cardAdded("/card_2", "B");
    bt.cardAdded("/card_1", "A");
    EXPECT_EQ(bt.card(), "/card_1");
    EXPECT_EQ(bt.address(), "A");
    bt.cardRemoved("/card_1");
    EXPECT_EQ(bt.card(), "/card_2");
}

TEST(BtCall, NotOutsideACall)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_FALSE(bt.wanted(idle));
    EXPECT_EQ(bt.sync(idle), Action::None);
}

TEST(BtCall, ACallGoesToAConnectedHeadset)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_TRUE(bt.wanted(call));
    EXPECT_TRUE(bt.wanted(voip));
    EXPECT_FALSE(bt.routed(call));       // not before the link is up
    bringUp(bt, call);
    EXPECT_TRUE(bt.routed(call));
    EXPECT_EQ(bt.scoCard(), "/card_1");
    EXPECT_EQ(bt.sync(call), Action::None);  // nothing more to do
}

TEST(BtCall, OneConnectAtATime)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_EQ(bt.sync(call), Action::Connect);
    EXPECT_EQ(bt.sync(call), Action::None);
    EXPECT_EQ(bt.sco(), Sco::Connecting);
}

TEST(BtCall, SpeakerAndHandsetKeepItOff)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_FALSE(bt.wanted(callSpeaker));
    EXPECT_FALSE(bt.wanted(callWired));
    bt.chose(Choice::Handset);
    EXPECT_FALSE(bt.wanted(call));
    bt.chose(Choice::Default);
    EXPECT_TRUE(bt.wanted(call));
}

TEST(BtCall, MovingOffTheHeadsetClosesTheLink)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    EXPECT_EQ(bt.sync(callSpeaker), Action::Disconnect);
    EXPECT_EQ(bt.sco(), Sco::Down);
    EXPECT_EQ(bt.scoCard(), "");
    EXPECT_EQ(bt.sync(callSpeaker), Action::None);
    EXPECT_EQ(bt.sync(call), Action::Connect);   // speaker off: back on it
}

TEST(BtCall, TheCallEndingClosesTheLink)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    EXPECT_EQ(bt.sync(idle), Action::Disconnect);
}

TEST(BtCall, LastConnectedWinsAgainstTheJack)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.jackPlugged();                    // the plug came after the headset
    EXPECT_FALSE(bt.wanted(callJack));
    EXPECT_TRUE(bt.wanted(call));        // the plug out again
    bt.cardAdded("/card_2", "B");        // a headset after the plug
    EXPECT_TRUE(bt.wanted(callJack));
    EXPECT_TRUE(bt.wanted(callBtJack));  // and chosen, whatever the order
}

TEST(BtCall, ChoosingTheHeadsetBeatsThePlug)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.jackPlugged();
    EXPECT_TRUE(bt.wanted(callBtJack));
}

TEST(BtCall, ANewDeviceUndoesAnExplicitHandset)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.chose(Choice::Handset);
    EXPECT_FALSE(bt.wanted(call));
    bt.cardAdded("/card_2", "B");
    EXPECT_TRUE(bt.wanted(call));
    bt.chose(Choice::Handset);
    bt.jackPlugged();                    // clears it, and the plug wins anyway
    EXPECT_FALSE(bt.wanted(callJack));
}

TEST(BtCall, AFailedLinkIsNotRetriedInTheSameCall)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_EQ(bt.sync(call), Action::Connect);
    bt.connectFailed();
    EXPECT_TRUE(bt.failed());
    EXPECT_EQ(bt.sco(), Sco::Down);
    EXPECT_FALSE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::None);
    EXPECT_FALSE(bt.wanted(callBt));     // still failed until chosen again
    bt.chose(Choice::Bluetooth);
    EXPECT_FALSE(bt.failed());
    EXPECT_EQ(bt.sync(callBt), Action::Connect);
}

TEST(BtCall, FailureIsForgottenAtTheEndOfTheCall)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.sync(call);
    bt.connectFailed();
    bt.callEnded();
    EXPECT_TRUE(bt.wanted(call));
}

TEST(BtCall, AFailureOnlyCountsForALinkAskedFor)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.connectFailed();
    EXPECT_FALSE(bt.failed());
    bringUp(bt, call);
    bt.connectFailed();
    EXPECT_EQ(bt.sco(), Sco::Up);
}

TEST(BtCall, ANewHeadsetGetsAFreshChance)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.sync(call);
    bt.connectFailed();
    bt.cardAdded("/card_2", "B");
    EXPECT_TRUE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::Connect);
    EXPECT_EQ(bt.scoCard(), "/card_2");
}

TEST(BtCall, TheHeadsetGivingTheAudioBackKeepsItOff)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    bt.scoClosed(true);
    EXPECT_TRUE(bt.givenBack());
    EXPECT_EQ(bt.sco(), Sco::Down);
    EXPECT_FALSE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::None);
    EXPECT_TRUE(bt.wanted(callBt));      // picked again on the screen
    bt.chose(Choice::Bluetooth);
    EXPECT_FALSE(bt.givenBack());
    bt.callEnded();
    EXPECT_TRUE(bt.wanted(call));
}

TEST(BtCall, ClosingItOurselvesIsNotAGiveBack)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    bt.scoClosed(false);
    EXPECT_FALSE(bt.givenBack());
    bt.scoClosed(true);                  // already down: nothing to give back
    EXPECT_FALSE(bt.givenBack());
}

TEST(BtCall, TheHeadsetButtonTakesTheCall)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.chose(Choice::Handset);
    const BtCall::Situation handset{eCallMode_Carrier, Choice::Default, false};
    EXPECT_FALSE(bt.wanted(handset));
    EXPECT_EQ(bt.scoConnected("/card_1", handset), Arrival::ByHeadset);
    EXPECT_EQ(bt.sco(), Sco::Up);
    EXPECT_EQ(bt.scoCard(), "/card_1");
    EXPECT_TRUE(bt.routed(callBt));      // the manager records the choice
}

TEST(BtCall, TheHeadsetButtonUndoesAGiveBackAndAFailure)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.sync(call);
    bt.connectFailed();
    EXPECT_EQ(bt.scoConnected("/card_1", call), Arrival::ByHeadset);
    EXPECT_FALSE(bt.failed());
    EXPECT_TRUE(bt.routed(callBt));
}

TEST(BtCall, NoLinkOutsideACallOrFromAStranger)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_EQ(bt.scoConnected("/card_1", idle), Arrival::Unwanted);
    EXPECT_EQ(bt.sco(), Sco::Down);
    EXPECT_EQ(bt.scoConnected("/card_7", call), Arrival::Unwanted);
    EXPECT_EQ(bt.sco(), Sco::Down);
}

TEST(BtCall, AStrangerDoesNotUndoAConnectInFlight)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bt.sync(call);
    EXPECT_EQ(bt.scoConnected("/card_7", call), Arrival::Unwanted);
    EXPECT_EQ(bt.sco(), Sco::Connecting);
    EXPECT_EQ(bt.scoConnected("/card_1", call), Arrival::Asked);
}

TEST(BtCall, ALinkThatComesTooLateIsClosed)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    EXPECT_EQ(bt.sync(call), Action::Connect);
    // The user put the speaker on while it was being opened.
    EXPECT_EQ(bt.sync(callSpeaker), Action::None);
    EXPECT_EQ(bt.scoConnected("/card_1", callSpeaker), Arrival::Unwanted);
    EXPECT_EQ(bt.sco(), Sco::Down);
}

TEST(BtCall, TheHeadsetGoingTakesTheLinkWithIt)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    EXPECT_TRUE(bt.cardRemoved("/card_1"));
    EXPECT_EQ(bt.sco(), Sco::Down);
    EXPECT_FALSE(bt.wanted(call));
    EXPECT_EQ(bt.sync(call), Action::None);
}

TEST(BtCall, AnotherHeadsetGoingKeepsTheLink)
{
    BtCall bt;
    bt.cardAdded("/card_2", "B");
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    bt.cardRemoved("/card_2");
    EXPECT_EQ(bt.sco(), Sco::Up);
    EXPECT_TRUE(bt.routed(call));
}

TEST(BtCall, ALaterHeadsetTakesOverTheLink)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    bt.cardAdded("/card_2", "B");
    EXPECT_EQ(bt.sync(call), Action::Disconnect);
    EXPECT_EQ(bt.sync(call), Action::Connect);
    EXPECT_EQ(bt.scoCard(), "/card_2");
}

TEST(BtCall, OfonoGoingClearsEverything)
{
    BtCall bt;
    bt.cardAdded("/card_1", "A");
    bringUp(bt, call);
    bt.serviceGone();
    EXPECT_FALSE(bt.available());
    EXPECT_EQ(bt.sco(), Sco::Down);
    EXPECT_EQ(bt.scoCard(), "");
}

} // namespace
