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

#ifndef _VOLUME_TARGET_H
#define _VOLUME_TARGET_H

#include <string>

/*
 * Which output a master/setVolume request moves, kept apart from the bus so
 * it can be tested.
 *
 * The LuneOS shell and apps name pcm_output, the phone's own output, for
 * "the volume". With a Bluetooth headset carrying the audio that volume is
 * the headset's (bluetooth_speaker0): setting pcm_output there changed
 * nothing anyone could hear. The other way round, a client still naming the
 * headset after it went - the shell follows the outputs getVolume reports -
 * gets the output that is active now instead of an error.
 *
 * Whether the headset carries the audio is what the legacy manager routes
 * by, not only the master volume manager's own active flag: that one is
 * missed when the headset was connected before audiod started, which left
 * the volume on pcm_output. With a wired headset in as well, Bluetooth still
 * has the music, so it has the volume too.
 *
 * A USB headset is the same: while it is the active output, pcm_output means
 * it, and a client naming it after it was unplugged gets the active output.
 */
namespace VolumeTarget
{
    //! Whether name is one of the Bluetooth outputs (bluetooth_speaker0, ...).
    bool isBluetoothOutput(const std::string &name);

    //! Whether name is one of the USB headset outputs (usb_speaker0, ...).
    //! Like a Bluetooth headset, a USB one has a sink and a volume of its own,
    //! and it is what the volume keys have to move while it plays the audio.
    bool isUsbOutput(const std::string &name);

    //! The Bluetooth output's name; audiod has one for whichever headset.
    extern const char kBluetoothOutput[];

    //! The output a request for requested moves, with active the display's
    //! active output, requestedConnected whether requested is connected and
    //! bluetoothAudio whether a Bluetooth headset carries the audio.
    std::string output(const std::string &requested, const std::string &active,
                       bool requestedConnected, bool bluetoothAudio);

    //! The output getVolume reports when the client names none, with active
    //! the display's active output and bluetoothAudio whether a Bluetooth
    //! headset carries the audio: the output the volume keys move.
    std::string reported(const std::string &active, bool bluetoothAudio);
}

#endif // _VOLUME_TARGET_H
