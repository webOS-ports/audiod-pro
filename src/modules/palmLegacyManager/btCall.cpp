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

#include "btCall.h"

bool BtCall::cardAdded(const std::string &path, const std::string &remote)
{
    if (path.empty() || mCards.count(path))
        return false;

    /* With the setting off, a headset that connects during a call is
     * connected, but the call stays where it is: nothing the user chose
     * for this call is undone either. */
    if (mInCall && !mTakesCallOnConnect)
    {
        mCards[path] = Card{remote, ++mSeq, true};
        return true;
    }

    mCards[path] = Card{remote, ++mSeq, false};

    /* A headset that has just connected is the newest thing: it takes the
     * call from the earpiece and the jack, also from an explicit earpiece
     * choice, and gets a fresh chance at a link that failed before. */
    mHandsetChosen = false;
    mGivenBack = false;
    mFailed = false;
    return true;
}

bool BtCall::cardRemoved(const std::string &path)
{
    if (!mCards.erase(path))
        return false;

    if (path == mScoCard)
    {
        /* The link went with the headset; there is nothing left to close. */
        mSco = Sco::Down;
        mScoCard.clear();
    }
    return true;
}

void BtCall::serviceGone()
{
    mCards.clear();
    mSco = Sco::Down;
    mScoCard.clear();
}

uint64_t BtCall::newestCardSeq() const
{
    uint64_t newest = 0;
    for (const auto &c : mCards)
    {
        if (!c.second.heldBack && c.second.seq > newest)
            newest = c.second.seq;
    }
    return newest;
}

std::string BtCall::callCard() const
{
    const std::string *best = nullptr;
    uint64_t newest = 0;
    for (const auto &c : mCards)
    {
        if (!c.second.heldBack && (!best || c.second.seq > newest))
        {
            best = &c.first;
            newest = c.second.seq;
        }
    }
    return best ? *best : std::string();
}

std::string BtCall::card() const
{
    const std::string *best = nullptr;
    uint64_t newest = 0;
    for (const auto &c : mCards)
    {
        if (!best || c.second.seq > newest)
        {
            best = &c.first;
            newest = c.second.seq;
        }
    }
    return best ? *best : std::string();
}

std::string BtCall::address() const
{
    const std::string path = card();
    auto it = mCards.find(path);
    return it == mCards.end() ? std::string() : it->second.remote;
}

void BtCall::jackPlugged()
{
    mJackSeq = ++mSeq;
    mHandsetChosen = false;
}

void BtCall::chose(Choice choice)
{
    mHandsetChosen = (choice == Choice::Handset);
    if (choice == Choice::Bluetooth)
    {
        mGivenBack = false;
        mFailed = false;
        /* Picking the headset picks the one connected last, also one kept
         * out of the call so far. */
        for (auto &c : mCards)
            c.second.heldBack = false;
    }
}

bool BtCall::wanted(const Situation &s) const
{
    if (s.mode == eCallMode_None || callCard().empty() || mFailed)
        return false;

    switch (s.choice)
    {
        case Choice::Bluetooth:
            return true;
        case Choice::Default:
            if (mHandsetChosen || mGivenBack)
                return false;
            /* Last connected wins: a headset connected after the plug went
             * in takes the call, a plug put in after the headset keeps it. */
            return !s.jackPlugged || newestCardSeq() > mJackSeq;
        case Choice::Handset:
        case Choice::Speaker:
        default:
            return false;
    }
}

BtCall::Action BtCall::sync(const Situation &s)
{
    mInCall = (s.mode != eCallMode_None);
    const bool want = wanted(s);

    if (want && mSco == Sco::Down)
    {
        mSco = Sco::Connecting;
        mScoCard = callCard();
        return Action::Connect;
    }

    if (want && mSco == Sco::Up && mScoCard != callCard())
    {
        /* Another headset connected since: last connected wins, so drop
         * this link; the next sync() opens one to the new headset. */
        mSco = Sco::Down;
        mScoCard.clear();
        return Action::Disconnect;
    }

    if (!want && mSco == Sco::Up)
    {
        mSco = Sco::Down;
        mScoCard.clear();
        return Action::Disconnect;
    }

    return Action::None;
}

BtCall::Arrival BtCall::scoConnected(const std::string &path, const Situation &s)
{
    const bool asked = (mSco == Sco::Connecting && path == mScoCard);

    if (asked)
    {
        if (!wanted(s))
        {
            /* The user moved the call elsewhere while the link was being
             * opened. */
            mSco = Sco::Down;
            mScoCard.clear();
            return Arrival::Unwanted;
        }
        mSco = Sco::Up;
        return Arrival::Asked;
    }

    /* The headset opened it: its button moves the call onto it, which is a
     * choice like picking it on the screen. Outside a call there is nothing
     * to carry, and an unknown card is not one of our headsets. */
    if (s.mode == eCallMode_None || !mCards.count(path))
    {
        if (mSco != Sco::Connecting)
        {
            mSco = Sco::Down;
            mScoCard.clear();
        }
        return Arrival::Unwanted;
    }

    chose(Choice::Bluetooth);
    mSco = Sco::Up;
    mScoCard = path;
    return Arrival::ByHeadset;
}

void BtCall::connectFailed()
{
    if (mSco != Sco::Connecting)
        return;

    mSco = Sco::Down;
    mScoCard.clear();
    mFailed = true;
}

void BtCall::scoClosed(bool byHeadset)
{
    const bool wasUp = (mSco == Sco::Up);

    mSco = Sco::Down;
    mScoCard.clear();

    /* The headset handing the audio back to the phone: the call stays off
     * it until the user picks it again. */
    if (wasUp && byHeadset)
        mGivenBack = true;
}

void BtCall::callEnded()
{
    mHandsetChosen = false;
    mGivenBack = false;
    mFailed = false;
    mInCall = false;
    for (auto &c : mCards)
        c.second.heldBack = false;
}
