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

#include "a2dpRestore.h"

void A2dpRestore::pulseGone(const std::vector<std::pair<std::string, std::string>> &connected,
                            std::uint64_t now)
{
    /* Going again before it was back adds to those it took the last time:
     * their A2DP may still be about to go. */
    if (mPulseUp)
        mHadA2dp.clear();
    mPulseUp = false;
    mGoneAt = now;
    for (const auto &device : connected)
    {
        if (!device.first.empty())
            mHadA2dp[device.first] = device.second;
    }
}

bool A2dpRestore::pulseBack()
{
    mPulseUp = true;
    return pending();
}

bool A2dpRestore::a2dpGone(const std::string &address, std::uint64_t now)
{
    auto it = mHadA2dp.find(address);
    if (it == mHadA2dp.end())
        return false;
    const std::string adapter = it->second;
    mHadA2dp.erase(it);
    if (now < mGoneAt || now - mGoneAt > DROP_WINDOW_MS)
        return false;
    Candidate candidate;
    candidate.adapter = adapter;
    mCandidates[address] = candidate;
    return true;
}

void A2dpRestore::a2dpBack(const std::string &address)
{
    /* A headset still on mHadA2dp has not lost A2DP yet (a2dpGone takes it
     * off): this is bluetooth2 repeating that it is connected, as it did
     * right after PulseAudio went on 2026-10-31 09:24:26. Its drop that
     * follows is still PulseAudio's. */
    mCandidates.erase(address);
}

std::vector<std::pair<std::string, std::string>> A2dpRestore::due()
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!mPulseUp)
        return out;
    for (auto &it : mCandidates)
    {
        Candidate &candidate = it.second;
        if (candidate.inFlight || candidate.attempts >= MAX_ATTEMPTS)
            continue;
        candidate.inFlight = true;
        candidate.attempts++;
        out.emplace_back(it.first, candidate.adapter);
    }
    return out;
}

bool A2dpRestore::failed(const std::string &address)
{
    auto it = mCandidates.find(address);
    if (it == mCandidates.end())
        return false;
    it->second.inFlight = false;
    if (it->second.attempts < MAX_ATTEMPTS)
        return true;
    mCandidates.erase(it);
    return false;
}

void A2dpRestore::done(const std::string &address)
{
    mCandidates.erase(address);
}

bool A2dpRestore::wanted(const std::string &address) const
{
    return mCandidates.count(address) != 0;
}

bool A2dpRestore::pending() const
{
    for (const auto &it : mCandidates)
    {
        if (!it.second.inFlight && it.second.attempts < MAX_ATTEMPTS)
            return true;
    }
    return false;
}
