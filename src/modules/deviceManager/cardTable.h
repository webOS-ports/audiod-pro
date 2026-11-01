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

#ifndef _CARD_TABLE_H
#define _CARD_TABLE_H

/*
 * DeviceManager's cards, kept by card id: whether PulseAudio has each one.
 *
 * isConnected says that audiod loaded the card in PulseAudio, or announced
 * a preloaded one as there, and keeps it from doing so twice. A PulseAudio
 * that has gone took every card with it: forgetLoadedCards() marks them all
 * as not loaded, so that the next connection loads and announces each of
 * them again, as at startup.
 *
 * Kept apart from the bus and from PulseAudio so it can be tested; CardMap
 * is utils::mapPhysicalInfo, or anything mapping keys to sequences of
 * cards with an isConnected member.
 */
template <typename CardMap>
void forgetLoadedCards(CardMap &cards)
{
    for (auto &entry : cards)
        for (auto &card : entry.second)
            card.isConnected = false;
}

/*
 * Calls load(card) for every card in the table, on the card in the table:
 * whatever load() records in it - that the card is now loaded - stays.
 */
template <typename CardMap, typename Load>
void forEachCard(CardMap &cards, Load load)
{
    for (auto &entry : cards)
        for (auto &card : entry.second)
            load(card);
}

#endif // _CARD_TABLE_H
