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

#include <gtest/gtest.h>

#include "btState.h"

namespace {

const std::string kAdapter = "00:00:46:01:2a:a1";
const std::string kOther = "00:00:46:01:2a:b2";
const std::string kHeadset = "74:1a:e0:20:8f:18";
const std::string kSpeaker = "aa:bb:cc:dd:ee:02";

// device/getStatus is made once per adapter, however often the adapter is
// reported - each report used to open another subscription only when the
// flag was never set again after it ended.
TEST(BtState, DeviceSubscriptionIsTakenOncePerAdapter)
{
    BtState state;
    EXPECT_TRUE(state.takeDeviceSubscription("sink:hci0"));
    EXPECT_FALSE(state.takeDeviceSubscription("sink:hci0"));
    EXPECT_FALSE(state.takeDeviceSubscription("sink:hci0"));
    EXPECT_TRUE(state.takeDeviceSubscription("sink:hci1"));
    EXPECT_FALSE(state.takeDeviceSubscription(""));
}

// What audiod did wrong when bluetooth2 restarted: an error reply (the
// adapter not powered yet) ended the subscription, and the next adapter
// report did not make it again.
TEST(BtState, AnErrorReplyEndsTheSubscriptionAndTheNextReportMakesItAgain)
{
    BtState state;
    ASSERT_TRUE(state.takeDeviceSubscription("sink:hci0"));
    state.deviceSubscriptionsEnded();
    EXPECT_TRUE(state.takeDeviceSubscription("sink:hci0"));

    ASSERT_TRUE(state.takeA2dpSubscription(kAdapter));
    EXPECT_FALSE(state.takeA2dpSubscription(kAdapter));
    state.a2dpSubscriptionsEnded();
    EXPECT_TRUE(state.takeA2dpSubscription(kAdapter));
}

TEST(BtState, Bluetooth2GoingEndsEverySubscriptionButKeepsTheDevices)
{
    BtState state;
    state.takeDeviceSubscription("sink:hci0");
    state.takeA2dpSubscription(kAdapter);
    state.connect(kHeadset, kAdapter);
    state.serviceDown();
    EXPECT_TRUE(state.takeDeviceSubscription("sink:hci0"));
    EXPECT_TRUE(state.takeA2dpSubscription(kAdapter));
    // its going does not drop a link: the next report says what is there
    EXPECT_TRUE(state.isConnected(kHeadset));
}

TEST(BtState, ADeviceConnectsAndDisconnectsOnce)
{
    BtState state;
    EXPECT_TRUE(state.connect(kHeadset, kAdapter));
    EXPECT_FALSE(state.connect(kHeadset, kAdapter)) << "a second load for the same headset";
    EXPECT_EQ(state.adapterOf(kHeadset), kAdapter);
    EXPECT_TRUE(state.disconnect(kHeadset));
    EXPECT_FALSE(state.disconnect(kHeadset)) << "a second unload";
    EXPECT_FALSE(state.isConnected(kHeadset));
    EXPECT_EQ(state.adapterOf(kHeadset), "");
    EXPECT_FALSE(state.connect("", kAdapter));
}

// A device/getStatus report is the adapter's whole device list: a headset
// it no longer lists with A2DP has gone, even if audiod missed it going.
TEST(BtState, AReportsMissingDevicesAreGone)
{
    BtState state;
    state.connect(kHeadset, kAdapter);
    state.connect(kSpeaker, kAdapter);
    EXPECT_EQ(state.gone(kAdapter, {kHeadset, kSpeaker}), std::vector<std::string>{});
    EXPECT_EQ(state.gone(kAdapter, {kSpeaker}), std::vector<std::string>{kHeadset});
    std::vector<std::string> all = state.gone(kAdapter, {});
    EXPECT_EQ(all.size(), 2u) << "an empty list is every device gone";
}

TEST(BtState, AReportOnlySpeaksForItsOwnAdapter)
{
    BtState state;
    state.connect(kHeadset, kAdapter);
    state.connect(kSpeaker, kOther);
    EXPECT_EQ(state.gone(kOther, {}), std::vector<std::string>{kSpeaker});
    EXPECT_EQ(state.gone(kAdapter, {kHeadset}), std::vector<std::string>{});
}

// PulseAudio restarting took the headsets' sinks with it: every headset
// still connected is loaded again, each once, on the adapter it is on.
TEST(BtState, EveryConnectedDeviceIsThereToLoadAgain)
{
    BtState state;
    EXPECT_TRUE(state.connectedDevices().empty());
    state.connect(kHeadset, kAdapter);
    state.connect(kSpeaker, kOther);
    state.connect(kHeadset, kAdapter);
    using Devices = std::vector<std::pair<std::string, std::string>>;
    EXPECT_EQ(state.connectedDevices(), (Devices{{kHeadset, kAdapter}, {kSpeaker, kOther}}));
    state.disconnect(kHeadset);
    EXPECT_EQ(state.connectedDevices(), (Devices{{kSpeaker, kOther}}));
    // bluetooth2 going does not drop a link, so it does not end a replay
    state.serviceDown();
    EXPECT_EQ(state.connectedDevices(), (Devices{{kSpeaker, kOther}}));
}

} // namespace
