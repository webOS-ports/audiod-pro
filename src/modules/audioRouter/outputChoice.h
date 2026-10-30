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

#ifndef _OUTPUT_CHOICE_H
#define _OUTPUT_CHOICE_H

#include <cstdint>
#include <string>
#include <vector>

/*
 * Which output a display's audio goes to, kept apart from the bus so it can
 * be tested.
 *
 * The outputs a person plugs in or pairs - the headphone jack, a Bluetooth
 * headset, a USB headset - are "removable": of those that are connected,
 * the one connected last carries the audio, as on Android
 * (LastRemovableMediaDevices). Plugging headphones in while a Bluetooth
 * headset plays moves the audio to the headphones, connecting the headset
 * again moves it back, and when the one in use goes the audio falls back to
 * the one connected before it. With no removable output connected, the
 * connected output with the best (lowest) priority number has it, which is
 * how audiod chose every output before.
 *
 * "Connected last" is a stamp taken when an output goes from disconnected
 * to connected. An output reported connected again while it already is -
 * PulseAudio restarting announces every sink anew - keeps its stamp, so a
 * restart does not reorder anything.
 */
namespace OutputChoice
{
    struct Output
    {
        std::string name;
        int priority = 0;
        bool removable = false;
        bool connected = false;
        //! When it was connected, from ConnectOrder::next(); 0 if never.
        std::uint64_t connectedAt = 0;
    };

    //! The output to use, or "" when none is connected.
    std::string choose(const std::vector<Output> &outputs);

    /* Hands out increasing connect stamps. 64 bits, so it does not wrap
     * on any target, armv7 included. */
    class ConnectOrder
    {
    public:
        //! The stamp an output gets when it goes from disconnected to
        //! connected; wasConnected true keeps the stamp it has.
        std::uint64_t stamp(bool wasConnected, std::uint64_t current);

    private:
        std::uint64_t mLast = 0;
    };
}

#endif // _OUTPUT_CHOICE_H
