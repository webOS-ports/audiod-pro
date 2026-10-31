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

#include "ringState.h"

#include <cstring>

bool RingState::isRingType(const char *mediaType)
{
    return mediaType && (!strcmp(mediaType, "pringtones") || !strcmp(mediaType, "palarm"));
}

bool RingState::isAlertType(const char *mediaType)
{
    return mediaType && (!strcmp(mediaType, "palerts") || !strcmp(mediaType, "pnotifications") ||
                         !strcmp(mediaType, "pcalendar"));
}

bool RingState::isMediaType(const char *mediaType)
{
    return mediaType && (!strcmp(mediaType, "pmedia") || !strcmp(mediaType, "pdefaultapp"));
}

bool RingState::streamSeen(uint32_t index, const char *mediaType, bool corked)
{
    const unsigned was = snapshot();

    mRinging.erase(index);
    mAlerting.erase(index);
    mMedia.erase(index);
    if (!corked)
    {
        if (isRingType(mediaType))
            mRinging.insert(index);
        else if (isAlertType(mediaType))
            mAlerting.insert(index);
        else if (isMediaType(mediaType))
            mMedia.insert(index);
    }

    return snapshot() != was;
}

bool RingState::streamGone(uint32_t index)
{
    const unsigned was = snapshot();
    mRinging.erase(index);
    mAlerting.erase(index);
    mMedia.erase(index);
    return snapshot() != was;
}

void RingState::clear()
{
    mRinging.clear();
    mAlerting.clear();
    mMedia.clear();
}

bool RingState::speakerWanted(bool ringerOn) const
{
    return ringing() || (ringerOn && alerting() && !mediaPlaying());
}

bool RingState::speakerToo(ECallMode mode, bool wanted)
{
    return wanted && mode == eCallMode_None;
}
