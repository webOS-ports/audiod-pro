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

#include <cstddef>
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
 * carries it, and lowers by kDefaultDuckDb unless the config says otherwise.
 */
namespace DuckPolicy
{
    //! A stream open on the mixer, by stream type, and whether it is muted.
    struct ActiveStream
    {
        std::string streamType;
        bool muted;
    };

    //! palm-policy's volume table 0, which every music stream uses, is
    //! linear in dB from 2 to 100: one step of the volume (0..100) is
    //! 0.404 dB, so 70 is 12.12 dB under 100. Lowering the volume by a
    //! number of steps lowers the music by the same dB at any volume.
    constexpr double kDbPerVolumeStep = 0.404;
    //! How far the music goes down when no config says (Droidian's
    //! module-role-ducking default; webOS 2.x used 12 dB, Android 14 dB).
    constexpr double kDefaultDuckDb = -20.0;
    //! The ramp takes this many steps, one every kRampStepMs.
    constexpr int kRampSteps = 6;
    constexpr unsigned kRampStepMs = 30;

    //! Whether streamType lowers the music while it plays.
    bool isDucking(const std::string &streamType);

    //! Whether streamType is music that is lowered.
    bool isDuckable(const std::string &streamType);

    //! Whether the music is to be lowered with these streams open: an
    //! audible ducking stream, with the ringer switch on.
    bool shouldDuck(const std::vector<ActiveStream> &active, bool ringerOn);

    //! How many volume steps lower the music by db (negative, or 0 for no
    //! ducking); a positive db is read as its negative.
    int levelForDb(double db);

    //! The level, in volume steps below its own volume, the music goes to.
    int targetLevel(bool ducked, int duckLevel);

    //! How far one ramp step moves the level, so a full duck takes kRampSteps.
    int rampStep(int duckLevel);

    //! The next ramp step from current towards target; target once there.
    int nextLevel(int current, int target, int step);

    //! volume (0..100) lowered by level steps; an audible stream stays
    //! audible, at 1 at least.
    int duckedVolume(int volume, int level);

    //! How long a sound on streamType waits before its samples start, so
    //! that it starts against music that is already down: the time the ramp
    //! still needs from currentLevel, plus one step for the mixer to report
    //! the new stream. Nothing when the sound does not duck, or the music is
    //! down already.
    unsigned leadInMs(const std::string &streamType, bool ringerOn,
                      int currentLevel, int duckLevel);

    //! Bytes of silence that play for ms at rate frames a second of
    //! frameBytes each.
    size_t silenceBytes(unsigned ms, unsigned rate, unsigned frameBytes);
}

#endif // _DUCK_POLICY_H
