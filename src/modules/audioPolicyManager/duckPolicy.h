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

#ifndef _DUCK_POLICY_H
#define _DUCK_POLICY_H

#include <string>
#include <vector>

/*
 * When the music is lowered under an alert, kept apart from the mixer so it
 * can be tested.
 *
 * webOS 2.x lowered media by 12 dB, ramped, while an alert, notification,
 * calendar or ringtone sound played with the ringer switch on
 * (MediaScenarioModule::programMediaVolumes). Here those arrive as palerts
 * (palm-policy routes pnotifications and pcalendar as palerts) and
 * pringtones (palarm too, so an alarm follows the ringer switch like a
 * ringtone; 2.x let an alarm duck regardless). 2.x left A2DP alone; LuneOS
 * ducks there as well, since the volume is set per stream on whatever sink
 * carries it.
 */
namespace DuckPolicy
{
    //! A stream open on the mixer, by stream type, and whether it is muted.
    struct ActiveStream
    {
        std::string streamType;
        bool muted;
    };

    //! What a ducked stream is played at, as a share of its volume: on
    //! palm-policy's volume table 70 is 12.1 dB under 100.
    constexpr int kDuckedPercent = 70;
    //! How far one ramp step moves the share, and how often.
    constexpr int kRampStepPercent = 5;
    constexpr unsigned kRampStepMs = 30;

    //! Whether streamType lowers the music while it plays.
    bool isDucking(const std::string &streamType);

    //! Whether streamType is music that is lowered.
    bool isDuckable(const std::string &streamType);

    //! Whether the music is to be lowered with these streams open: an
    //! audible ducking stream, with the ringer switch on.
    bool shouldDuck(const std::vector<ActiveStream> &active, bool ringerOn);

    //! The share, in percent, the music is played at when ducked or not.
    int targetPercent(bool ducked);

    //! The next ramp step from current towards target; target once there.
    int nextPercent(int current, int target);

    //! volume (0..100) played at percent of itself.
    int scaled(int volume, int percent);
}

#endif // _DUCK_POLICY_H
