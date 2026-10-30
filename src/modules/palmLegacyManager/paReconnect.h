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

#ifndef _PA_RECONNECT_H
#define _PA_RECONNECT_H

/*
 * When to open a new PulseAudio connection after the old one died.
 *
 * PA_CONTEXT_NOFAIL only makes the first connect wait for the daemon; a
 * context that has failed or terminated stays dead, so after PulseAudio
 * restarts the manager has to open a new one. The new context waits for the
 * daemon by itself, so the delay only keeps a context that keeps failing from
 * spinning: it starts short, doubles on every loss in a row, and starts over
 * once a connection has come up.
 *
 * Kept apart from PulseAudio so it can be tested.
 */
class PaReconnect
{
public:
    static constexpr unsigned kFirstDelayMs = 500;
    static constexpr unsigned kMaxDelayMs = 5000;

    //! The connection was lost, or could not be opened: how long to wait
    //! before opening a new one.
    unsigned lost();
    //! The connection came up: the next loss waits kFirstDelayMs again.
    void connected() { mLosses = 0; }

private:
    unsigned mLosses = 0;
};

#endif // _PA_RECONNECT_H
