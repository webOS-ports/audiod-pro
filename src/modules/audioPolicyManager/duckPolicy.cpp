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
#include <cmath>

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

int levelForDb(double db)
{
    const double below = db < 0 ? -db : db;
    const long level = std::lround(below / kDbPerVolumeStep);
    return static_cast<int>(std::min<long>(level, 98));
}

int targetLevel(bool ducked, int duckLevel)
{
    return ducked ? duckLevel : 0;
}

int rampStep(int duckLevel)
{
    return std::max(1, (duckLevel + kRampSteps - 1) / kRampSteps);
}

int nextLevel(int current, int target, int step)
{
    if (current < target)
        return std::min(current + step, target);
    if (current > target)
        return std::max(current - step, target);
    return target;
}

int duckedVolume(int volume, int level)
{
    if (volume <= 0)
        return volume;
    return std::max(volume - level, 1);
}

unsigned leadInMs(const std::string &streamType, bool ringerOn,
                  int currentLevel, int duckLevel)
{
    if (!isDucking(streamType) || !ringerOn || currentLevel >= duckLevel)
        return 0;
    const int step = rampStep(duckLevel);
    const int steps = (duckLevel - currentLevel + step - 1) / step;
    return static_cast<unsigned>(steps + 1) * kRampStepMs;
}

size_t silenceBytes(unsigned ms, unsigned rate, unsigned frameBytes)
{
    const size_t frames = static_cast<size_t>(rate) * ms / 1000;
    return frames * frameBytes;
}

}
