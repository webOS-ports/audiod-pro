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

#include "voipRoute.h"

#include <pulse/module-palm-policy.h>

const char *const VoipRoute::SPEAKER = "pcm_output";

ClassRoute::Redirect VoipRoute::callRedirect(ECallMode mode, const std::string &speakerMaster)
{
    ClassRoute::Redirect r;
    if (mode != eCallMode_Voip || speakerMaster.empty())
        return r;
    r.from = SPEAKER;
    r.to = speakerMaster;
    r.classes = {evoipcall, evoipcall1};
    return r;
}
