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

#include "callVolume.h"

size_t CallVolume::index(Route route)
{
    switch (route)
    {
        case Route::Speaker:    return 1;
        case Route::Wired:      return 2;
        case Route::Bluetooth:  return 3;
        case Route::Earpiece:
        default:                return 0;
    }
}

const char *CallVolume::name(Route route)
{
    static const char *const names[kRoutes] = { "earpiece", "speaker", "wired", "bluetooth" };
    return names[index(route)];
}

const char *CallVolume::settingKey(Route route)
{
    static const char *const keys[kRoutes] = {
        "callVolumeEarpiece", "callVolumeSpeaker", "callVolumeWired", "callVolumeBluetooth" };
    return keys[index(route)];
}

int CallVolume::volume(Route route) const
{
    return mVolume[index(route)].value();
}

bool CallVolume::stored(Route route, int volume)
{
    return mVolume[index(route)].stored(snap(volume));
}

bool CallVolume::set(Route route, int volume)
{
    return mVolume[index(route)].set(snap(volume));
}

bool CallVolume::offset(Route route, int offset)
{
    if (offset == 0)
        return false;

    /* Whole levels, rounded away from 0: a key's 10 is one level. */
    const int magnitude = (offset > 0) ? offset : -offset;
    const int levels = (magnitude + kStep - 1) / kStep;
    const int moved = volume(route) + ((offset > 0) ? levels : -levels) * kStep;
    return set(route, moved);
}

int CallVolume::snap(int volume)
{
    if (volume <= 0)
        return 0;
    if (volume >= 100)
        return 100;
    return ((volume + kStep / 2) / kStep) * kStep;
}

double CallVolume::halVoiceVolume(int volume)
{
    const int level = snap(volume);
    if (level >= 100)
        return 1.0;
    /* 0 -> 0.1, 20 -> 0.3, ... 80 -> 0.9: the middle of MediaTek's index
     * ranges [0, 0.2), [0.2, 0.4), ... The top level is 1.0, the only value
     * that reaches the last index. */
    return level / 100.0 + 0.5 * kStep / 100.0;
}
