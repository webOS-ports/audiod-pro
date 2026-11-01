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

#include "callMediaMute.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace CallMediaMute
{

namespace {

constexpr std::array<const char *, 11> kMuted = {
    "pmedia", "pdefaultapp", "default1", "default2", "btstream",
    "fm", "am", "hdradio", "radio",
    // module-palm-policy routes these as pmedia and pdefaultapp
    "pvvm", "pflash",
};

}

bool mutedInCall(const char *type)
{
    if (type == nullptr)
        return false;
    return std::any_of(kMuted.begin(), kMuted.end(),
                       [type](const char *muted) { return std::strcmp(type, muted) == 0; });
}

bool mutedInCall(const std::string &type)
{
    return mutedInCall(type.c_str());
}

bool effective(const std::string &type, bool userMuted, bool callUp)
{
    return userMuted || (callUp && mutedInCall(type));
}

bool muteNow(bool callUp, const char *mediaType, bool muted)
{
    return callUp && !muted && mutedInCall(mediaType);
}

}
