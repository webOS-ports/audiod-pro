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

#include "btState.h"

bool BtState::takeDeviceSubscription(const std::string &adapter)
{
    return !adapter.empty() && mDeviceSubscribed.insert(adapter).second;
}

bool BtState::takeA2dpSubscription(const std::string &adapter)
{
    return !adapter.empty() && mA2dpSubscribed.insert(adapter).second;
}

void BtState::deviceSubscriptionsEnded()
{
    mDeviceSubscribed.clear();
}

void BtState::a2dpSubscriptionsEnded()
{
    mA2dpSubscribed.clear();
}

void BtState::serviceDown()
{
    deviceSubscriptionsEnded();
    a2dpSubscriptionsEnded();
}

bool BtState::connect(const std::string &address, const std::string &adapter)
{
    if (address.empty())
        return false;
    return mConnected.emplace(address, adapter).second;
}

bool BtState::disconnect(const std::string &address)
{
    return mConnected.erase(address) != 0;
}

bool BtState::isConnected(const std::string &address) const
{
    return mConnected.count(address) != 0;
}

std::string BtState::adapterOf(const std::string &address) const
{
    auto it = mConnected.find(address);
    return it == mConnected.end() ? std::string() : it->second;
}

std::vector<std::pair<std::string, std::string>> BtState::connectedDevices() const
{
    return {mConnected.begin(), mConnected.end()};
}

std::vector<std::string> BtState::gone(const std::string &adapter,
                                       const std::set<std::string> &reported) const
{
    std::vector<std::string> out;
    for (const auto &device : mConnected) {
        if (device.second == adapter && reported.count(device.first) == 0)
            out.push_back(device.first);
    }
    return out;
}
