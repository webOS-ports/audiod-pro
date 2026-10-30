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

#include "volumeTarget.h"

namespace
{
    // The phone's own output; the one every LuneOS volume control names.
    const char kBuiltInOutput[] = "pcm_output";
}

const char VolumeTarget::kBluetoothOutput[] = "bluetooth_speaker0";

bool VolumeTarget::isBluetoothOutput(const std::string &name)
{
    return name.compare(0, 9, "bluetooth") == 0;
}

std::string VolumeTarget::output(const std::string &requested, const std::string &active,
                                 bool requestedConnected, bool bluetoothAudio)
{
    if (requested == kBuiltInOutput)
    {
        if (bluetoothAudio)
            return kBluetoothOutput;
        if (isBluetoothOutput(active))
            return active;
        return requested;
    }

    if (isBluetoothOutput(requested) && !requestedConnected && !bluetoothAudio)
        return active.empty() ? std::string(kBuiltInOutput) : active;

    return requested;
}
