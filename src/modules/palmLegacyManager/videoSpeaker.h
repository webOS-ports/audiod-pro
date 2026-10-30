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
#ifndef _VIDEO_SPEAKER_H
#define _VIDEO_SPEAKER_H

/*
 * Whether a video call should be on the loudspeaker (test plan E04).
 *
 * A video call is held at arm's length, where the earpiece cannot be heard,
 * so it starts on the loudspeaker unless a wired or Bluetooth headset is
 * there to take it. Android does the same (Telecom's
 * CallsManager.isSpeakerphoneAutoEnabledForVideoCalls, and its baseline
 * route skipping the earpiece for a video call), legacy webOS took the
 * earpiece scenario out of service while a VoIP call had video
 * (State::setCallWithVideo), and SailfishOS bars the earpiece for a call
 * that owns video playback.
 *
 * The loudspeaker chosen here is remembered as automatic, which is what
 * lets it be undone: when the video stops, or a headset arrives, the call
 * goes back to the earpiece (and from there to the headset). A speaker the
 * user picked is theirs and is never undone here, and once the user has
 * picked a route during a video call, nothing here moves the call again
 * until the next time the call turns into a video call. With the
 * speakerphone out of service (hearing aid mode) it is never chosen.
 *
 * Kept apart from the manager so it can be tested: the manager tells it
 * where the call is and what is connected, and moves the call as told.
 */
class VideoSpeaker
{
public:
    struct Situation
    {
        bool videoCall;   //!< the call being routed is a VoIP call with video
        bool onEarpiece;  //!< the call's chosen route is the earpiece
        bool onSpeaker;   //!< the call's chosen route is the loudspeaker
        bool headset;     //!< a wired or Bluetooth headset can take the call
        bool noSpeaker;   //!< the speakerphone is out of service (HAC)
    };

    enum Move
    {
        eMove_None = 0,
        eMove_Speaker,   //!< put the call on the loudspeaker
        eMove_Earpiece   //!< take it off again: back to the earpiece
    };

    //! Something about the call or the outputs changed: where should it go?
    Move update(const Situation &now);
    //! The user (or a client acting for them) picked the call's route.
    void userPicked();
    //! The last call ended: the next one starts afresh.
    void callEnded();

    //! Whether the call is on the loudspeaker because of this, not the user.
    bool automatic() const { return mAutomatic; }

private:
    bool mVideo = false;
    bool mAutomatic = false;
    bool mUserPicked = false;
};

#endif
