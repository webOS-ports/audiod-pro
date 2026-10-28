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

#include "callState.h"

#include <algorithm>

const char *const CallState::carrierTransport = "com.palm.telephony";

ECallStatus CallState::statusFromString(const std::string &state)
{
    if (state == "active")       return eCallStatus_Active;
    if (state == "incoming")     return eCallStatus_Incoming;
    if (state == "connecting")   return eCallStatus_Connecting;
    if (state == "dialing")      return eCallStatus_Dialing;
    if (state == "onHold")       return eCallStatus_OnHold;
    if (state == "disconnected") return eCallStatus_Disconnected;
    return eCallStatus_NoCall;
}

bool CallState::inCall(ECallStatus status)
{
    return status == eCallStatus_Active || status == eCallStatus_OnHold;
}

void CallState::setCarrierCall(bool inCall)
{
    mCarrierCall = inCall;
}

void CallState::report(const std::string &reporter, const std::vector<Line> &lines)
{
    if (lines.empty())
    {
        if (mReported.erase(reporter) == 0)
            mReported.clear();
        return;
    }

    /* Every line counts, not the last one: with one call active and a second
     * one just ended, the active one is still a call. */
    Calls calls;
    for (const Line &line : lines)
    {
        if (!inCall(line.status))
            continue;
        if (line.transport == carrierTransport)
        {
            calls.carrier = true;
        }
        else
        {
            calls.voip = true;
            calls.video = calls.video || line.video;
        }
    }
    mReported[reporter] = calls;
}

ECallMode CallState::mode() const
{
    bool voip = false;
    if (mCarrierCall)
        return eCallMode_Carrier;
    for (const auto &reported : mReported)
    {
        if (reported.second.carrier)
            return eCallMode_Carrier;
        voip = voip || reported.second.voip;
    }
    return voip ? eCallMode_Voip : eCallMode_None;
}

bool CallState::video() const
{
    return std::any_of(mReported.begin(), mReported.end(),
                       [](const auto &reported) { return reported.second.video; });
}

CallState::Transition CallState::transition(ECallMode from, ECallMode to)
{
    Transition t{};
    t.pauseMedia = (from == eCallMode_None && to != eCallMode_None);
    t.resumeMedia = (from != eCallMode_None && to == eCallMode_None);
    t.resetSpeaker = t.resumeMedia;
    return t;
}
