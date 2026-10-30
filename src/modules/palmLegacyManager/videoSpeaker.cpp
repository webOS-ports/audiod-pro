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
#include "videoSpeaker.h"

VideoSpeaker::Move VideoSpeaker::update(const Situation &now)
{
    /* Turning into a video call is a new reason to be on the loudspeaker,
     * whatever was picked while it was a voice call - as Android switches
     * to the speaker on every upgrade to video. */
    if (now.videoCall && !mVideo)
        mUserPicked = false;
    mVideo = now.videoCall;

    /* Something else took the call off the loudspeaker (HAC, a disabled
     * scenario): there is nothing of ours left to undo. */
    if (mAutomatic && !now.onSpeaker)
        mAutomatic = false;

    const bool wanted = now.videoCall && !now.headset && !now.noSpeaker && !mUserPicked;

    if (wanted && now.onEarpiece)
    {
        mAutomatic = true;
        return eMove_Speaker;
    }
    if (!wanted && mAutomatic)
    {
        mAutomatic = false;
        return eMove_Earpiece;
    }
    return eMove_None;
}

void VideoSpeaker::userPicked()
{
    mUserPicked = true;
    mAutomatic = false;
}

void VideoSpeaker::callEnded()
{
    mVideo = false;
    mAutomatic = false;
    mUserPicked = false;
}
