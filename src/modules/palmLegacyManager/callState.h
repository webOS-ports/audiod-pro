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

    //! When the music paused for a call plays again after it.
    enum class Resume
    {
        Never,      //!< nothing was paused for the call
        Now,
        Later       //!< after resumeDelayMs, see resumeAfterCall()
    };
    //! The last call ended. pausedForCall: media was paused for it.
    //! onHeadsetLink: the call was on a Bluetooth headset's SCO link, which
    //! audiod has only just closed.
    //!
    //! A "playing" reaching the headset (AVRCP) while that link is still
    //! going down made a headset (a Nuraphone, 2026-10-31) send a pause of
    //! its own some 50 ms later, which stopped the music again. The phone
    //! app's cellular calls did not show it because the Music app resumes
    //! those itself, on oFono's call end, before the call mode changes here.
    static Resume resumeAfterCall(bool pausedForCall, bool onHeadsetLink);
    //! How long Resume::Later waits: several times the 60-240 ms the headset
    //! took to send its pause.
    static constexpr unsigned resumeDelayMs = 1500;
    //! Whether a resume put off with Resume::Later is still to be done when
    //! its time comes: the music is still paused for the call (a headset
    //! that went meanwhile cancels it), and no new call has started (whose
    //! end resumes it instead).
    static bool resumeStillDue(bool pausedForCall, ECallMode mode);

    //! What has to happen when the headphones are unplugged.
    struct Unplug
    {
        bool pauseMedia;          //!< no call: the music would go on out loud
        bool keepPausedAfterCall; //!< in a call: resuming afterwards would be out loud
    };
    //! Nothing, when Bluetooth carries the audio: the plug was not playing it.
    static Unplug unplugged(ECallMode mode, bool bluetoothAudio);

    //! Whether name is an A2DP sink. PulseAudio 17 names them
    //! bluez_sink.<addr>.<profile>; newer ones use bluez_output.
    static bool isBluetoothOutput(const std::string &name);
    //! Whether name is a USB headset's sink. module-palm-policy names them
    //! after the routing config's usb_speaker entry: usb_speaker0, usb_speaker1.
    static bool isUsbOutput(const std::string &name);

    //! Whether what plays on sink stays private: a Bluetooth or USB
    //! headset's sink, or the built-in output with headphones in
    //! (jackPlugged: it plays into the plug). These are the outputs
    //! audioRouter counts as removable (OutputChoice): of them the one
    //! connected last has the audio, and when it goes the audio falls back
    //! to the one connected before it - only with none left, to the
    //! loudspeaker.
    static bool privateOutput(const std::string &sink, bool jackPlugged);

    //! What has to happen when the active output moves from one sink to
    //! another: going from a headset (Bluetooth or USB) to an output that
    //! is not private is an unplug, the same as pulling the jack. Going to
    //! another headset or to the jack is not; the music stays private. The
    //! one rule for both kinds of headset, and the jack's (unplugged() with
    //! privateOutput() of what has the audio after it) is the same.
    static Unplug outputMoved(ECallMode mode, const std::string &from,
                              const std::string &to, bool jackPlugged);

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
