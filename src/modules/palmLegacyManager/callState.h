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

#ifndef _CALL_STATE_H
#define _CALL_STATE_H

#include <map>
#include <string>
#include <vector>

/* Which transport a call is running over. Carrier calls route through the
 * modem's own voice path, VoIP calls stay on the normal PCM path, so the two
 * need different hardware routing. */
enum ECallMode
{
    eCallMode_None = 0,
    eCallMode_Carrier,
    eCallMode_Voip
};

/* The call states reported per line in phone/CallStatusUpdate. Mirrors
 * audiod-pro's own ECallStatus so the Gen 2 handling could be carried over
 * unchanged. */
enum ECallStatus
{
    eCallStatus_NoCall = 0,
    eCallStatus_Incoming,
    eCallStatus_Dialing,
    eCallStatus_Connecting,
    eCallStatus_Active,
    eCallStatus_OnHold,
    eCallStatus_Disconnected
};

/*
 * Every call audiod has been told about, and who told it, kept apart from
 * the bus so it can be tested.
 *
 * Calls are reported from more than one place. The phone app reports its
 * cellular calls through setCallMode. Each messaging connector (WhatsApp,
 * Telegram, Signal, Teams, ... in imlibpurpleservice) reports its own VoIP
 * call through phone/CallStatusUpdate, with its own transport on the line.
 * A CallStatusUpdate is the whole truth for the one who sent it, and only
 * for them: a connector hanging up ({"lines":[]}) must not end a cellular
 * call, nor another connector's call.
 *
 * A reporter that has never sent a line (a one-shot luna-send, which is a
 * new sender every time) saying "no calls" ends every call reported through
 * CallStatusUpdate, which is what that meant when the phone app was the only
 * one sending it. Connectors that share a process (imlibpurpleservice) share
 * a sender, so two VoIP calls on two networks at once are one reporter.
 */
class CallState
{
public:
    struct Line
    {
        ECallStatus status;
        std::string transport;
        bool video;
    };

    //! The transport of the carrier (modem) stack; anything else is VoIP.
    static const char *const carrierTransport;

    static ECallStatus statusFromString(const std::string &state);
    //! Whether a call in this state needs its audio path: talking or on hold.
    static bool inCall(ECallStatus status);

    //! The phone app's cellular call is (no longer) in progress.
    void setCarrierCall(bool inCall);
    //! A reporter's phone/CallStatusUpdate: all of its lines.
    void report(const std::string &reporter, const std::vector<Line> &lines);

    //! What the calls need: a carrier call wins over VoIP.
    ECallMode mode() const;
    //! Whether a call in progress carries video.
    bool video() const;

    //! What has to happen when the call mode changes.
    struct Transition
    {
        bool pauseMedia;    //!< the first call started
        bool resumeMedia;   //!< the last call ended
        bool resetSpeaker;  //!< the last call ended: the next starts on the earpiece
        bool resetMute;     //!< the last call ended: the mic is live again
    };
    static Transition transition(ECallMode from, ECallMode to);

    //! What has to happen when the headphones are unplugged.
    struct Unplug
    {
        bool pauseMedia;          //!< no call: the music would go on out loud
        bool keepPausedAfterCall; //!< in a call: resuming afterwards would be out loud
    };
    //! Nothing, when Bluetooth carries the audio: the plug was not playing it.
    static Unplug unplugged(ECallMode mode, bool bluetoothAudio);

private:
    struct Calls
    {
        bool carrier = false;
        bool voip = false;
        bool video = false;
    };

    bool mCarrierCall = false;
    std::map<std::string, Calls> mReported;
};

#endif // _CALL_STATE_H
