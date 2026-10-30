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

#ifndef _BT_CALL_H
#define _BT_CALL_H

#include <cstdint>
#include <map>
#include <string>

#include "callState.h"

/*
 * Call audio on a Bluetooth hands-free (HFP) headset.
 *
 * oFono is the audio gateway: it answers the headset's service level
 * connection, runs the AT commands (answer, hang up, ring, indicators) and
 * shows each connected headset as a HandsfreeAudioCard. The call audio
 * itself needs an SCO link to the headset, which oFono opens when its audio
 * agent asks - HfpAgent - and hands over as a socket. On a Halium phone the
 * sound does not go through that socket: the Android audio HAL moves it
 * between the modem and the Bluetooth chip once its output is "BT SCO", so
 * holding the socket open is what keeps the link up, and routing the droid
 * card to its BT SCO ports is what puts the call on it.
 *
 * This class is the bookkeeping and the policy, kept apart from D-Bus and
 * PulseAudio so it can be tested: which headsets are connected, the state of
 * the link, and whether the call should be on the headset.
 *
 * The policy, as Android's and SailfishOS' phones behave:
 *  - a call goes to a connected headset unless the user picked the handset or
 *    the loudspeaker;
 *  - whatever was connected last wins between the headset and the jack;
 *  - the headset taking the audio itself (its button) moves the call onto it,
 *    and the headset giving it back moves the call off it for the rest of that
 *    call, until the user picks the headset again;
 *  - a link that could not be opened is not tried again in the same call,
 *    unless the user picks the headset again or another headset connects;
 *  - nothing is on the headset outside a call: the ringtone rings on the
 *    phone and the headset rings by itself on oFono's RING.
 */
class BtCall
{
public:
    enum class Sco
    {
        Down,
        Connecting,     //!< asked oFono for the link, no socket yet
        Up              //!< holding the socket
    };

    //! What to do about the link after a change, see sync().
    enum class Action
    {
        None,
        Connect,        //!< ask oFono to open the link to card()
        Disconnect      //!< close the socket
    };

    //! What the phone app (or the Palm scenario) chose for the call.
    enum class Choice
    {
        Default,        //!< nothing in particular: the earpiece, the jack or the headset
        Handset,        //!< the earpiece, explicitly, also with a headset connected
        Speaker,        //!< the loudspeaker
        Bluetooth       //!< the headset
    };

    struct Situation
    {
        ECallMode mode;
        Choice choice;
        bool jackPlugged;
    };

    //! oFono reported a headset (a HandsfreeAudioCard of type "gateway").
    //! True if it was not known before.
    bool cardAdded(const std::string &path, const std::string &remote);
    //! A headset went. True if it was known.
    bool cardRemoved(const std::string &path);
    //! oFono went away, and every card and link with it.
    void serviceGone();

    bool available() const { return !mCards.empty(); }
    //! The headset to use: the one connected last, "" if none.
    std::string card() const;
    //! Its Bluetooth address, "" if none.
    std::string address() const;

    //! Something was plugged into the jack: from now on it is newer than
    //! every headset connected so far.
    void jackPlugged();

    //! The user chose: Bluetooth undoes an earlier give-back or failure in
    //! this call, Handset keeps a connected headset out of it.
    void chose(Choice choice);

    //! Whether the call should be on the headset.
    bool wanted(const Situation &s) const;
    //! Whether it is: wanted, and the link is up.
    bool routed(const Situation &s) const { return wanted(s) && mSco == Sco::Up; }

    //! Bring the link in line with wanted(): the state moves on as if the
    //! returned action had been done. A link still being opened is not
    //! cancelled; scoConnected() tells what to do with it when it comes.
    Action sync(const Situation &s);

    //! What scoConnected() found.
    enum class Arrival
    {
        Asked,          //!< the link sync() asked for
        ByHeadset,      //!< the headset opened it (its button, or AT+BCC)
        Unwanted        //!< not wanted (any more): close it again
    };
    //! oFono handed over the socket of a link to path.
    Arrival scoConnected(const std::string &path, const Situation &s);
    //! The link asked for could not be opened.
    void connectFailed();
    //! The link closed. byHeadset: the headset (or the radio) closed it, not
    //! audiod.
    void scoClosed(bool byHeadset);
    //! The last call ended: the next one starts from the default again.
    void callEnded();

    Sco sco() const { return mSco; }
    //! The card the link is to, "" when there is none.
    const std::string &scoCard() const { return mScoCard; }
    bool failed() const { return mFailed; }
    bool givenBack() const { return mGivenBack; }

private:
    struct Card
    {
        std::string remote;
        uint64_t seq;       //!< when it connected
    };

    std::map<std::string, Card> mCards;     // path -> card
    uint64_t mSeq = 0;
    uint64_t mJackSeq = 0;
    Sco mSco = Sco::Down;
    std::string mScoCard;
    bool mHandsetChosen = false;
    bool mGivenBack = false;
    bool mFailed = false;

    uint64_t newestCardSeq() const;
};

#endif // _BT_CALL_H
