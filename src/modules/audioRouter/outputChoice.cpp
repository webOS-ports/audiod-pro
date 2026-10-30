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

#include "outputChoice.h"

std::string OutputChoice::choose(const std::vector<Output> &outputs)
{
    const Output *latest = nullptr;
    const Output *best = nullptr;

    for (const Output &output : outputs)
    {
        if (!output.connected)
            continue;
        if (output.removable)
        {
            if (!latest || output.connectedAt > latest->connectedAt)
                latest = &output;
        }
        else if (!best || output.priority < best->priority)
        {
            best = &output;
        }
    }

    if (latest)
        return latest->name;
    return best ? best->name : std::string();
}

std::uint64_t OutputChoice::ConnectOrder::stamp(std::uint64_t current)
{
    if (current != 0)
        return current;
    return ++mLast;
}
