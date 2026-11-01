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

#ifndef _BT_STATE_H
#define _BT_STATE_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

/*
 * What BluetoothManager knows of bluetooth2, kept apart from the bus so it
 * can be tested: which subscriptions are open on it, and which devices
 * have A2DP to us connected.
 *
 * A subscription ends when bluetooth2 answers it with an error (as it does
 * for device/getStatus on an adapter that is not powered yet) or when
 * bluetooth2 goes away; then it has to be made again, and the next
 * adapter/getStatus report does that. A connected device stays connected
 * until bluetooth2 says otherwise - also across bluetooth2 restarting,
 * which does not drop a link - and each device/getStatus report of an
 * adapter is the whole truth for it: a device it no longer lists with
 * A2DP has gone.
 */
class BtState
{
public:
    //! Whether device/getStatus has to be subscribed for this adapter now;
    //! if so it counts as subscribed from here on.
    bool takeDeviceSubscription(const std::string &adapter);
    //! Whether a2dp/getStatus has to be subscribed for this adapter now;
    //! if so it counts as subscribed from here on.
    bool takeA2dpSubscription(const std::string &adapter);
    //! The device/getStatus subscriptions ended (an error reply).
    void deviceSubscriptionsEnded();
    //! The a2dp/getStatus subscriptions ended (an error reply).
    void a2dpSubscriptionsEnded();
    //! bluetooth2 went away: every subscription on it ended.
    void serviceDown();

    //! A device has A2DP to us; true if it was not connected before.
    bool connect(const std::string &address, const std::string &adapter);
    //! A device's A2DP went; true if it was connected.
    bool disconnect(const std::string &address);
    bool isConnected(const std::string &address) const;
    //! The adapter a connected device is on, or "".
    std::string adapterOf(const std::string &address) const;
    //! Every connected device, as (address, adapter): what PulseAudio has
    //! to be told again when it has restarted, since its Bluetooth module
    //! and the headsets' sinks went with it.
    std::vector<std::pair<std::string, std::string>> connectedDevices() const;

    //! adapter's whole device list says these have A2DP to us: the
    //! devices on that adapter that were connected and are not among them.
    std::vector<std::string> gone(const std::string &adapter,
                                  const std::set<std::string> &reported) const;

private:
    std::set<std::string> mDeviceSubscribed;
    std::set<std::string> mA2dpSubscribed;
    std::map<std::string, std::string> mConnected; // address -> adapter
};

#endif // _BT_STATE_H
