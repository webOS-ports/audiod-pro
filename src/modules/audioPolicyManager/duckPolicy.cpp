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

#include "duckPolicy.h"

#include <algorithm>

namespace DuckPolicy
{

bool isDucking(const std::string &streamType)
{
    return streamType == "palerts" || streamType == "pringtones";
}

bool isDuckable(const std::string &streamType)
{
    // pdefaultapp is where every stream that names no sink goes, music
    // players included; btstream is A2DP sink audio played back here.
    return streamType == "pmedia" || streamType == "pdefaultapp" ||
           streamType == "btstream";
}

bool shouldDuck(const std::vector<ActiveStream> &active, bool ringerOn)
{
    if (!ringerOn)
        return false;
    return std::any_of(active.begin(), active.end(), [](const ActiveStream &s) {
        return isDucking(s.streamType) && !s.muted;
    });
}

int targetPercent(bool ducked)
{
    return ducked ? kDuckedPercent : 100;
}

int nextPercent(int current, int target)
{
    if (current < target)
        return std::min(current + kRampStepPercent, target);
    if (current > target)
        return std::max(current - kRampStepPercent, target);
    return target;
}

int scaled(int volume, int percent)
{
    return (volume * percent) / 100;
}

}
