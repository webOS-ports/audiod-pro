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

#include <map>
#include <string>
#include <vector>

#include "cardTable.h"

namespace {

// The members of utils::CARD_INFO_T that the table works on.
struct Card
{
    std::string name;
    int cardNumber = -1;
    bool isConnected = false;
};
using Cards = std::map<std::string, std::vector<Card>>;

Cards phone()
{
    Cards cards;
    cards[""] = {{"pcm_output", 0, true}, {"pcm_input", 0, true}};
    cards["USB"] = {{"usb_speaker", 2, true}};
    return cards;
}

// PulseAudio going takes every card with it, the preloaded pcm_output too:
// each has to be loaded or announced again on the next connection.
TEST(CardTable, PulseAudioGoingForgetsEveryCard)
{
    Cards cards = phone();
    forgetLoadedCards(cards);
    for (const auto &entry : cards)
        for (const auto &card : entry.second)
            EXPECT_FALSE(card.isConnected) << card.name;
    EXPECT_EQ(cards.at("").size(), 2u) << "the cards themselves stay known";
    EXPECT_EQ(cards.at("USB").at(0).cardNumber, 2);
}

TEST(CardTable, ForgettingAnEmptyTableDoesNothing)
{
    Cards cards;
    forgetLoadedCards(cards);
    EXPECT_TRUE(cards.empty());
}

// What a load records stays in the table. Walked on copies, as the
// connection handler did, a card loaded on one connection was not recorded
// as loaded, so whether a reconnect announced it again depended on whether
// PDM had reported it before or after the first connection.
TEST(CardTable, ALoadIsRecordedInTheTable)
{
    Cards cards = phone();
    forgetLoadedCards(cards);
    int loads = 0;
    forEachCard(cards, [&loads](Card &card) {
        if (!card.isConnected) {
            card.isConnected = true;
            ++loads;
        }
    });
    EXPECT_EQ(loads, 3);
    forEachCard(cards, [&loads](const Card &card) {
        if (!card.isConnected)
            ++loads;
    });
    EXPECT_EQ(loads, 3) << "a second connection loads nothing twice";
}

} // namespace
