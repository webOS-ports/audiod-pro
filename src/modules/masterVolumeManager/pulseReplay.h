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

#ifndef _PULSE_REPLAY_H
#define _PULSE_REPLAY_H

#include <set>
#include <string>

/*
 * Which outputs have to be given their volume and mute again.
 *
 * A PulseAudio that restarts comes up with every sink at its own default:
 * full volume, not muted. audiod still has what the user set, and the
 * output is announced again once audiod has reconnected; that is when it
 * has to be sent. Only after PulseAudio was lost, and once per output and
 * daemon: at startup the volumes come from the settings database instead,
 * and audiod has nothing of its own to send yet.
 *
 * Kept apart from the bus and from PulseAudio so it can be tested.
 */
class PulseReplay
{
public:
    //! PulseAudio has gone, and the volumes and mutes of its sinks with it.
    void pulseLost();
    //! output was announced as connected: whether its volume and mute have
    //! to be sent to PulseAudio again. If so, they count as sent.
    bool takeReplay(const std::string &output);

private:
    bool mLost = false;
    std::set<std::string> mReplayed;
};

#endif // _PULSE_REPLAY_H
