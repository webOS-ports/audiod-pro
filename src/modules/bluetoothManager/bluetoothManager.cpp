// Copyright (c) 2020-2023 LG Electronics, Inc.
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

#include "bluetoothManager.h"

BluetoothManager* BluetoothManager::mBluetoothManager = nullptr;
bool BluetoothManager::mIsObjRegistered = BluetoothManager::RegisterObject();

BluetoothManager* BluetoothManager::getBluetoothManagerInstance()
{
    return mBluetoothManager;
}

bool BluetoothManager::readBluetoothConfigurationInfo()
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "BluetoothManager::readBluetoothConfigurationInfo");
    bool loadStatus = true;
    std::stringstream jsonFilePath;
    jsonFilePath << CONFIG_DIR_PATH << "/" << BLUETOOTH_CONFIG;

    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "Loading bluetooth configuration info from json file %s",\
        jsonFilePath.str().c_str());
    fileJsonBluetoothConfig = pbnjson::JDomParser::fromFile(jsonFilePath.str().c_str(),\
        pbnjson::JSchema::AllSchema());
    if (!fileJsonBluetoothConfig.isValid() || !fileJsonBluetoothConfig.isObject())
    {
        PM_LOG_ERROR(MSGID_INVALID_INPUT, INIT_KVCOUNT,\
            "readBluetoothConfigurationInfo : Failed to parse json config file. File: %s",\
            jsonFilePath.str().c_str());
        loadStatus = false;
        return false;
    }

    if (loadStatus)
    {
        if (initializeBluetoothConfigurationInfo())
            PM_LOG_INFO(MSGID_POLICY_MANAGER, INIT_KVCOUNT, "initializeBluetoothConfigurationInfo success");
        else
        {
            PM_LOG_ERROR(MSGID_POLICY_MANAGER, INIT_KVCOUNT, "initializeBluetoothConfigurationInfo failed");
            return false;
        }
    }
    return true;
}

bool BluetoothManager::initializeBluetoothConfigurationInfo()
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "BluetoothManager::initializeBluetoothConfigurationInfo");
    pbnjson::JValue bluetoothConfigurationInfo = fileJsonBluetoothConfig["configuration"];
    if (!bluetoothConfigurationInfo.isArray())
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "bluetoothConfigurationInfo is not an array");
        return false;
    }
    else
    {
        for (const pbnjson::JValue& elements : bluetoothConfigurationInfo.items())
        {
            int displays = elements["displays"].asNumber<int>();
            if (elements["adapterNameKey"].asString(mAdapterNameKey) == CONV_OK)
                PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,"BluetoothManager::initializeBluetoothConfigurationInfo: Successfully read adapterNameKey %s",
                mAdapterNameKey.c_str());
            else
            {
                PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,"BluetoothManager::initializeBluetoothConfigurationInfo: Failed to read adapterNameKey");
                return false;
            }

            if (elements["a2dpAdapterName"].asString(mA2dpAdapterName) == CONV_OK)
                PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,"BluetoothManager::initializeBluetoothConfigurationInfo: Successfully read a2dpAdapterName %s", mA2dpAdapterName.c_str());
            else
            {
                PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,"BluetoothManager::initializeBluetoothConfigurationInfo: Failed to read a2dpAdapterName");
                return false;
            }

            if (elements.hasKey("adapterNames"))
            {
                PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "found adapterNames key");
                JValue adaptersInfo = elements["adapterNames"];
                if (!adaptersInfo.isArray())
                {
                    PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "adaptersInfo is not an array");
                    return false;
                }
                else
                {
                    std::string adapterName;
                    for (auto &element:adaptersInfo.items())
                    {
                        adapterName = element.asString();
                        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "adapterName %s", adapterName.c_str());
                    }
                }
            }
            else
            {
                PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "adapterNames key not found");
                return false;
            }
            for (int displayindex=1; displayindex<=displays; displayindex++)
                mDisplayConnectionStatus.insert(std::pair<int, bool>(displayindex, false));
        }
    }
    return true;
}

/* A reply from bluetooth2, read leniently: only the fields used are looked
 * at, each checked for its type, so an error reply (which carries
 * serviceName and nothing else expected) or a field bluetooth2 adds later
 * is read rather than refused - and a reply is never replied to. */
static pbnjson::JValue btReply(LSMessage *message, const char *caller)
{
    const char *payload = LSMessageGetPayload(message);
    pbnjson::JValue reply = pbnjson::JDomParser::fromString(payload ? payload : "");
    if (!reply.isObject())
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s: reply is not a JSON object", caller);
        return pbnjson::JValue();
    }
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "%s: got '%s'", caller, payload);
    return reply;
}

static bool btBool(const pbnjson::JValue &reply, const char *name)
{
    return reply[name].isBoolean() && reply[name].asBool();
}

static std::string btString(const pbnjson::JValue &reply, const char *name)
{
    return reply[name].isString() ? reply[name].asString() : std::string();
}

int BluetoothManager::displayOfAdapter(const std::string &adapterAddress) const
{
    auto it = mAdapterDisplayIDMap.find(adapterAddress);
    return it == mAdapterDisplayIDMap.end() ? 0 : it->second;
}

void BluetoothManager::setBlueToothA2DPActive(bool state, const std::string &address,
                                              const std::string &profile, const std::string &adapterAddress)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
        "%s : state = %d, address = %s, profile = %s, adapterAddress = %s", \
        __FUNCTION__, state, address.c_str(), profile.c_str(), adapterAddress.c_str());
    int display = displayOfAdapter(adapterAddress);

    if (state)
    {
        if (!mBtState.connect(address, adapterAddress))
        {
            /* bluetooth2 repeating itself: the headset did not come back,
             * so a drop of it that is to be undone still is. */
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s BT device is already in connected state",  __FUNCTION__);
            return;
        }
        mA2dpRestore.a2dpBack(address);
        auto mapIter = mDisplayConnectionStatus.find(display);
        if (mapIter != mDisplayConnectionStatus.end())
            mapIter->second = true;
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "Display value is %d", display);
        if (mObjAudioMixer)
        {
            mObjAudioMixer->programLoadBluetooth(address.c_str(), profile.c_str(), display);
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
                "%s : loaded bluetooth device",  __FUNCTION__);
        }
        if (mObjModuleManager)
        {
            events::EVENT_MASTER_VOLUME_STATUS_T eventMasterVolumeStatus;
            eventMasterVolumeStatus.eventName = utils::eEventMasterVolumeStatus;
            mObjModuleManager->publishModuleEvent((events::EVENTS_T*)&eventMasterVolumeStatus);
        }
        else
            PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                       "mObjModuleManager is NULL");
    }
    else
    {
        if (!mBtState.disconnect(address))
            return;
        if (mA2dpRestore.a2dpGone(address, static_cast<std::uint64_t>(g_get_monotonic_time() / 1000)))
        {
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s: A2DP of %s went with PulseAudio; connecting it again once PulseAudio is back",
                __FUNCTION__, address.c_str());
            scheduleA2dpRestore();
        }
        auto mapIter = mDisplayConnectionStatus.find(display);
        if (mapIter != mDisplayConnectionStatus.end())
            mapIter->second = false;
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "Display value is %d", display);
        if (mObjAudioMixer)
        {
            mObjAudioMixer->programUnloadBluetooth(profile.c_str(), display);
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s : unloaded bluetooth device",  __FUNCTION__);
        }
    }
    if (mObjModuleManager && display != 0)
    {
        events::EVENT_BT_DEVICE_DISPAY_INFO_T stEventBtDeviceDisplayInfo;
        stEventBtDeviceDisplayInfo.eventName = utils::eEventBTDeviceDisplayInfo;
        stEventBtDeviceDisplayInfo.displayId = display;
        stEventBtDeviceDisplayInfo.address = address;
        stEventBtDeviceDisplayInfo.state = state;
        mObjModuleManager->publishModuleEvent((events::EVENTS_T*)&stEventBtDeviceDisplayInfo);
    }
}

void BluetoothManager::btAdapterQueryInfo(LSMessage *message)
{
    pbnjson::JValue reply = btReply(message, __FUNCTION__);
    if (reply.isNull())
        return;
    if (!btBool(reply, "returnValue"))
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s: adapter/getStatus failed: %s", __FUNCTION__, btString(reply, "errorText").c_str());
        return;
    }

    pbnjson::JValue adapters = reply["adapters"];
    for (int i = 0; adapters.isArray() && i < adapters.arraySize(); i++)
    {
        std::string adapterName = btString(adapters[i], mAdapterNameKey.c_str());
        std::string adapterAddress = btString(adapters[i], "adapterAddress");
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "adapterAddress = %s, a2dpAdapterName = %s, adapterName = %s",
            adapterAddress.c_str(), mA2dpAdapterName.c_str(), adapterName.c_str());
        if (adapterName.empty() || adapterAddress.empty())
            continue;

        if (adapterName == "hci0")
            mAdapterDisplayIDMap[adapterAddress] = 1;
        else if (adapterName == "hci1")
            mAdapterDisplayIDMap[adapterAddress] = 2;

        /* device/getStatus once per adapter, made again when it ended -
         * as it does when bluetooth2 restarts, or refuses it for an
         * adapter that is not powered yet; this report comes again when
         * the adapter changes. */
        bool a2dpSource = (mA2dpAdapterName == adapterName);
        if (!mBtState.takeDeviceSubscription((a2dpSource ? "source:" : "sink:") + adapterName))
        {
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s: Already subscribed for adapter: %s", __FUNCTION__, adapterAddress.c_str());
            continue;
        }
        std::string payload = string_printf("{\"adapterAddress\":\"%s\",\"subscribe\":true}", adapterAddress.c_str());
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s : payload = %s", __FUNCTION__, payload.c_str());
        mObjModuleManager->subscribeKeyInfo(this, a2dpSource ? eEventA2DPDeviceStatus : eEventBTDeviceStatus,
            eBluetoothService2, BT_DEVICE_GET_STATUS, payload);
    }
}

void BluetoothManager::btDeviceGetStatusInfo (LSMessage *message)
{
    pbnjson::JValue reply = btReply(message, __FUNCTION__);
    if (reply.isNull())
        return;
    if (!btBool(reply, "returnValue"))
    {
        /* The subscription is over; the next adapter report makes it again. */
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s device status subscription failed: %s", __FUNCTION__, btString(reply, "errorText").c_str());
        mBtState.deviceSubscriptionsEnded();
        return;
    }

    std::string replyAdapter = btString(reply, "adapterAddress");
    std::set<std::string> reported;
    pbnjson::JValue devices = reply["devices"];
    for (int i = 0; devices.isArray() && i < devices.arraySize(); i++)
    {
        pbnjson::JValue device = devices[i];
        pbnjson::JValue profiles = device["connectedProfiles"];
        bool a2dp = false;
        for (int j = 0; profiles.isArray() && j < profiles.arraySize(); j++)
            a2dp = a2dp || (profiles[j].isString() && profiles[j].asString() == "a2dp");
        if (!a2dp)
            continue;

        /* A device we are the A2DP sink of (a phone playing to us), or
         * the hands-free gateway, is not a headset to play to. */
        bool sink = true;
        pbnjson::JValue roles = device["connectedRoles"];
        for (int k = 0; roles.isArray() && k < roles.arraySize(); k++)
        {
            std::string role = roles[k].isString() ? roles[k].asString() : std::string();
            if (role == "A2DP_SRC" || role == "HFP_AG")
                sink = false;
        }
        std::string address = btString(device, "address");
        std::string adapterAddress = btString(device, "adapterAddress");
        if (adapterAddress.empty())
            adapterAddress = replyAdapter;
        if (!sink || address.empty())
        {
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s: %s is not a headset to play to", __FUNCTION__, address.c_str());
            continue;
        }
        reported.insert(address);
        if (replyAdapter.empty())
            replyAdapter = adapterAddress;

        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "Send info to PA for loading the bluez module, address = %s", address.c_str());
        setBlueToothA2DPActive(true, address, "a2dp", adapterAddress);

        if (mBtState.takeA2dpSubscription(adapterAddress))
        {
            std::string payload = string_printf("{\"adapterAddress\":\"%s\",\"address\":\"%s\",\"subscribe\":true}",
                adapterAddress.c_str(), address.c_str());
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "a2dp/getStatus payload = %s", payload.c_str());
            mObjModuleManager->subscribeKeyInfo(this, eLunaEventA2DPStatus,
                eBluetoothService2, BT_A2DP_GET_STATUS, payload);
        }
    }

    /* The whole list of the adapter: whatever was connected on it and is
     * not in it any more has gone - also while audiod was not told, as when
     * a subscription had ended. */
    for (const std::string &address : mBtState.gone(replyAdapter, reported))
    {
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s: %s no longer has A2DP", __FUNCTION__, address.c_str());
        setBlueToothA2DPActive(false, address, "a2dp", replyAdapter);
    }
}

void BluetoothManager::btA2DPGetStatusInfo (LSMessage *message)
{
    pbnjson::JValue reply = btReply(message, __FUNCTION__);
    if (reply.isNull())
        return;
    if (!btBool(reply, "returnValue"))
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s a2dp status subscription failed: %s", __FUNCTION__, btString(reply, "errorText").c_str());
        mBtState.a2dpSubscriptionsEnded();
        return;
    }

    std::string address = btString(reply, "address");
    std::string adapterAddress = btString(reply, "adapterAddress");
    bool connected = btBool(reply, "connected");
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "Device MAC address %s Connection Status %d adapter address %s",\
            address.c_str(), connected, adapterAddress.c_str());
    if (address.empty())
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "Device MAC address field not found");
        return;
    }
    setBlueToothA2DPActive(connected, address, "a2dp", adapterAddress);
}

void BluetoothManager::setBluetoothA2DPSource(bool state)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "%s : state = %d", __FUNCTION__, state);
    if (mObjAudioMixer && mObjAudioMixer->programA2dpSource(state))
    {
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "Sending programA2dpSource to PA is success");
    }
}

void BluetoothManager::btA2DPSourceGetStatus(LSMessage *message)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "%s ", __FUNCTION__);

    LSMessageJsonParser msg(message,SCHEMA_9(REQUIRED(subscribed, boolean),
    REQUIRED(adapterAddress, string),REQUIRED(returnValue, boolean),REQUIRED
    (connecting, boolean),REQUIRED(connected, boolean),REQUIRED(playing, boolean),
    REQUIRED(address, string),OPTIONAL(errorCode, integer),OPTIONAL(errorText, string)));

    if (!msg.parse(__FUNCTION__))
        return;
    bool returnValue = false;

    msg.get("returnValue", returnValue);

    if (returnValue)
    {
        std::string a2dpDeviceAddress;
        std::string adapterAddress;
        bool connected = false;
        msg.get("address", a2dpDeviceAddress);
        msg.get("adapterAddress", adapterAddress);
        /* A reply without "connected" says nothing about the link: asBool()
         * would have read it as false and taken the A2DP source down. */
        if (!msg.get("connected", connected))
        {
            PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s : a2dp status without connected, ignored", __FUNCTION__);
            return;
        }
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "Device MAC address %s Connection Status %d adapter address %s", a2dpDeviceAddress.c_str(), connected, adapterAddress.c_str());
        setBluetoothA2DPSource(connected);
    }
    else
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s : a2dp status subscription failed", __FUNCTION__);
    }
}

void BluetoothManager::a2dpDeviceGetStatus (LSMessage *message)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "%s ", __FUNCTION__);

    std::string payload = LSMessageGetPayload(message);

    LSMessageJsonParser msg(message,SCHEMA_6(REQUIRED(subscribed, boolean),
    REQUIRED(adapterAddress, string),REQUIRED(returnValue, boolean),REQUIRED
    (devices,array),OPTIONAL(errorCode, integer),OPTIONAL(errorText, string)));
    if (!msg.parse(__FUNCTION__))
        return;
    bool returnValue = false;
    msg.get("returnValue", returnValue);

    if (returnValue)
    {
        pbnjson::JValue deviceStatus = msg.get();
        pbnjson::JValue devices = deviceStatus["devices"];
        if (!devices.isArray())
            return;

        for (int i = 0; i < devices.arraySize(); i++)
        {
            pbnjson::JValue connectedProfiles = devices[i]["connectedProfiles"];
            std::string profile;
            std::string role;
            std::string address;
            std::string adapterAddress;
            if (0 == connectedProfiles.arraySize())
                continue;

            for (int j = 0; j < connectedProfiles.arraySize(); j++)
            {
                pbnjson::JValue connectedRoles = devices[i]["connectedRoles"];
                for (int k = 0; k < connectedRoles.arraySize(); k++)
                {
                    role = connectedRoles[k].asString();
                    if ("A2DP_SRC" == role)
                    {
                        mA2dpSource = true;
                        break;
                    }
                    mA2dpSource = false;
                }
                profile = connectedProfiles[j].asString();
                PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                    "Getting connectedProfiles and connectedRole: %s %s",profile.c_str(), role.c_str());
                if (("a2dp" == profile) && mA2dpSource)
                {
                    address = devices[i]["address"].asString();
                    adapterAddress = devices[i]["adapterAddress"].asString();
                    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                        "Send info to PA for a2dpSource %d", mA2dpSource);
                    setBluetoothA2DPSource(true);
                    std::string payload = string_printf("{\"adapterAddress\":\"%s\",\"address\":\"%s\",\"subscribe\":true}",adapterAddress.c_str(), address.c_str());
                    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                        "%s : payload = %s", __FUNCTION__, payload.c_str());
                    mObjModuleManager->subscribeKeyInfo(this, eEventA2DPSourceStatus, eBluetoothService2,
                        BT_A2DP_GET_STATUS, payload);
                    break;
                }
            }
        }
    }
}

void BluetoothManager::eventKeyInfo (LUNA_KEY_TYPE_E type, LSMessage *message)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "%s LUNA_KEY_TYPE_E = %d", __FUNCTION__, (int)type);
    if (type == eEventBTAdapter)
    {
        btAdapterQueryInfo(message);
    }
    else if (type == eEventBTDeviceStatus)
    {
        btDeviceGetStatusInfo(message);
    }
    else if (type == eLunaEventA2DPStatus)
    {
        btA2DPGetStatusInfo(message);
    }
    else if (type == eEventA2DPDeviceStatus)
    {
        a2dpDeviceGetStatus(message);
    }
    else if (type == eEventA2DPSourceStatus)
    {
        btA2DPSourceGetStatus(message);
    }
}

void BluetoothManager::eventServerStatusInfo(SERVER_TYPE_E serviceName, bool connected)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
        "Got BT server status event %d : %d", serviceName, connected);
    if (serviceName != eBluetoothService2)
        return;
    if (connected)
    {
        /* lunaEventSubscriber makes adapter/getStatus again; its report
         * makes the device subscriptions again. */
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "BT service is connected");
    }
    else
    {
        /* Every subscription on bluetooth2 ended with it. The devices stay
         * as they were - its going does not drop a link - until its next
         * report says otherwise. */
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "BT service went away");
        mBtState.serviceDown();
    }
}

BluetoothManager::BluetoothManager(ModuleConfig* const pConfObj):
    mA2dpSource(false), mDisplayID(0)
{
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
        "BT manager constructor");
    mObjAudioMixer = AudioMixer::getAudioMixerInstance();
    if (!mObjAudioMixer)
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
            "AudioMixer instance is null");
    }
    mObjModuleManager = ModuleManager::getModuleManagerInstance();
    if (!mObjModuleManager)
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
            "mObjModuleManager instance is null");
    }
    else
        mObjModuleManager->subscribeModuleEvent(this, utils::eEventMixerStatus);
}

void BluetoothManager::eventMixerStatus(bool mixerStatus, utils::EMIXER_TYPE mixerType)
{
    if (utils::ePulseMixer != mixerType)
        return;
    if (!mixerStatus)
    {
        /* PulseAudio's A2DP endpoints went with it, and BlueZ takes the
         * headsets' A2DP down when they do: see a2dpRestore.h. */
        mA2dpRestore.pulseGone(mBtState.connectedDevices(),
                               static_cast<std::uint64_t>(g_get_monotonic_time() / 1000));
        return;
    }
    if (mA2dpRestore.pulseBack())
        scheduleA2dpRestore();
    if (!mObjAudioMixer)
        return;
    /* PulseAudio is (back) up. One that restarted lost the Bluetooth module
     * audiod had it load, and the headsets' sinks with it; one that came up
     * after a headset connected never had them. Have it load them for every
     * headset still connected: their sinks come back, module-palm-policy
     * announces them, and audioRouter routes to them again. */
    for (const auto &device : mBtState.connectedDevices())
    {
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s: loading bluetooth again for %s", __FUNCTION__, device.first.c_str());
        mObjAudioMixer->programLoadBluetooth(device.first.c_str(), "a2dp", displayOfAdapter(device.second));
    }
}

void BluetoothManager::scheduleA2dpRestore()
{
    if (mA2dpRestoreTimer || !mA2dpRestore.pulseUp() || !mA2dpRestore.pending())
        return;
    mA2dpRestoreTimer = g_timeout_add_seconds(A2DP_RESTORE_DELAY_SECONDS,
                                              &BluetoothManager::a2dpRestoreTimedOut, this);
}

gboolean BluetoothManager::a2dpRestoreTimedOut(gpointer data)
{
    BluetoothManager *self = static_cast<BluetoothManager*>(data);
    self->mA2dpRestoreTimer = 0;
    for (const auto &device : self->mA2dpRestore.due())
    {
        pbnjson::JValue payload = pbnjson::Object();
        payload.put("address", device.first);
        if (!device.second.empty())
            payload.put("adapterAddress", device.second);
        PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
            "%s: connecting A2DP of %s again: %s", __FUNCTION__, device.first.c_str(),
            payload.stringify().c_str());
        std::string *address = new std::string(device.first);
        CLSError lserror;
        if (!LSCallOneReply(GetPalmService(), BT_A2DP_CONNECT, payload.stringify().c_str(),
                            &BluetoothManager::a2dpConnectReply, address, nullptr, &lserror))
        {
            lserror.Print(__FUNCTION__, __LINE__);
            delete address;
            self->mA2dpRestore.failed(device.first);
        }
    }
    self->scheduleA2dpRestore();
    return G_SOURCE_REMOVE;
}

bool BluetoothManager::a2dpConnectReply(LSHandle *sh, LSMessage *reply, void *ctx)
{
    (void)sh;
    std::string *address = static_cast<std::string*>(ctx);
    BluetoothManager *self = mBluetoothManager;
    /* The headset may have A2DP again by itself meanwhile: then nothing
     * waits for this answer. */
    if (self && address && self->mA2dpRestore.wanted(*address))
    {
        pbnjson::JValue answer = btReply(reply, __FUNCTION__);
        /* 128 "Device is already connecting" may still fail, so it is
         * waited for and tried again; 131 "Already connected" is what was
         * wanted. */
        const int errorCode = answer["errorCode"].isNumber() ? answer["errorCode"].asNumber<int>() : 0;
        if (btBool(answer, "returnValue") || errorCode == 131)
            self->mA2dpRestore.done(*address);
        else if (self->mA2dpRestore.failed(*address))
        {
            PM_LOG_WARNING(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s: connecting A2DP of %s failed (%d), trying again", __FUNCTION__,
                address->c_str(), errorCode);
            self->scheduleA2dpRestore();
        }
        else
            PM_LOG_WARNING(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "%s: connecting A2DP of %s failed (%d), giving up", __FUNCTION__,
                address->c_str(), errorCode);
    }
    delete address;
    return true;
}

BluetoothManager::~BluetoothManager()
{
    if (mA2dpRestoreTimer)
        g_source_remove(mA2dpRestoreTimer);
    PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
        "BT manager destructor");
}

void BluetoothManager::initialize()
{
    if (mBluetoothManager->readBluetoothConfigurationInfo())
    {
        if (mBluetoothManager)
        {
            mBluetoothManager->mObjModuleManager->subscribeServerStatusInfo(mBluetoothManager, \
                eBluetoothService2);
            mBluetoothManager->mObjModuleManager->subscribeKeyInfo(mBluetoothManager, eEventBTAdapter, \
                eBluetoothService2, BT_ADAPTER_GET_STATUS, BT_ADAPTER_SUBSCRIBE_PAYLOAD);
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
                "Successfully initialized BluetoothManager");
        }
        else
        {
            PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
                "mBluetoothManager is nullptr");
        }
    }
    else
    {
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, \
                "Failed initializing BluetoothManager");
    }
}

void BluetoothManager::deInitialize()
{
    PM_LOG_DEBUG("BluetoothManager deInitialize()");
    if (mBluetoothManager)
    {
        delete mBluetoothManager;
        mBluetoothManager = nullptr;
    }
    else
        PM_LOG_ERROR(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT, "mBluetoothManager is nullptr");
}

void BluetoothManager::handleEvent(events::EVENTS_T* event)
{
    switch((int)event->eventName)
    {
        case utils::eEventServerStatusSubscription:
        {
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                    "handleEvent:: eEventServerStatusSubscription");
            events::EVENT_SERVER_STATUS_INFO_T *serverStatusInfoEvent = (events::EVENT_SERVER_STATUS_INFO_T*)event;
            eventServerStatusInfo(serverStatusInfoEvent->serviceName, serverStatusInfoEvent->connectionStatus);
        }
        break;
        case utils::eEventMixerStatus:
        {
            const auto *mixerStatusEvent = reinterpret_cast<const events::EVENT_MIXER_STATUS_T *>(event);
            eventMixerStatus(mixerStatusEvent->mixerStatus, mixerStatusEvent->mixerType);
        }
        break;
        case eEventBTDeviceStatus:
        case eEventA2DPDeviceStatus:
        case eLunaEventA2DPStatus:
        case eEventA2DPSourceStatus:
        case eEventBTAdapter:
        {
            PM_LOG_INFO(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                    "handleEvent:: Calling eventKeyInfo");
            events::EVENT_KEY_INFO_T *keyInfoEvent = (events::EVENT_KEY_INFO_T*)event;
            eventKeyInfo(keyInfoEvent->type, keyInfoEvent->message);
        }
        break;
        default:
        {
            PM_LOG_WARNING(MSGID_BLUETOOTH_MANAGER, INIT_KVCOUNT,\
                "handleEvent:Unknown event");
        }
        break;
    }
}
