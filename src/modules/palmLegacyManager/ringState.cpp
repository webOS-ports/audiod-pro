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

bool RingState::streamSeen(uint32_t index, const char *mediaType, bool corked)
{
    const bool was = ringing();

    if (isRingType(mediaType) && !corked)
        mRinging.insert(index);
    else
        mRinging.erase(index);

    return ringing() != was;
}

bool RingState::streamGone(uint32_t index)
{
    const bool was = ringing();
    mRinging.erase(index);
    return ringing() != was;
}

void RingState::clear()
{
    mRinging.clear();
}

bool RingState::speakerToo(ECallMode mode, bool ringing)
{
    return ringing && mode == eCallMode_None;
}
