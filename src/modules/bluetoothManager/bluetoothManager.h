/* @@@LICENSE
*
*      Copyright (c) 2020-2021 LG Electronics Company.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#ifndef _BLUETOOTH_MANAGER_H
#define _BLUETOOTH_MANAGER_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glib.h>

#include "utils.h"
#include "messageUtils.h"
#include "audioMixer.h"
#include "log.h"
#include "moduleFactory.h"
#include "btState.h"
#include "a2dpRestore.h"

#define BT_ADAPTER_SUBSCRIBE_PAYLOAD "{\"subscribe\":true}"
#define BT_DEVICE_GET_STATUS    "luna://com.webos.service.bluetooth2/device/getStatus"
#define BT_A2DP_GET_STATUS      "luna://com.webos.service.bluetooth2/a2dp/getStatus"
#define BT_ADAPTER_GET_STATUS   "luna://com.webos.service.bluetooth2/adapter/getStatus"
#define BT_A2DP_CONNECT         "luna://com.webos.service.bluetooth2/a2dp/connect"

typedef std::map<std::string, bool> adapterInfo;
typedef std::map<std::string, int> adapterDisplayIDMap;
typedef std::map<std::string, int> adapterAddressDisplayIDMap;
typedef std::map<int, bool> displayConnectionStatus;
typedef std::vector<std::string> connectedAddressVector;

#define CONFIG_DIR_PATH "/etc/palm/audiod"

#define BLUETOOTH_CONFIG "bluetooth_configuration.json"

class BluetoothManager : public ModuleInterface
{
    private:

        bool mA2dpSource;
        int mDisplayID;
        BtState mBtState;
        /* Headsets whose A2DP went with PulseAudio, to connect again once
         * it is back: see a2dpRestore.h. */
        A2dpRestore mA2dpRestore;
        //! Gives PulseAudio's new endpoints time to be registered with
        //! BlueZ before A2DP is connected to them (PipeWire waits 2 s too).
        static constexpr guint A2DP_RESTORE_DELAY_SECONDS = 2;
        guint mA2dpRestoreTimer = 0;
        void scheduleA2dpRestore();
        static gboolean a2dpRestoreTimedOut(gpointer data);
        static bool a2dpConnectReply(LSHandle *sh, LSMessage *reply, void *ctx);
        std::string mAdapterNameKey;
        std::string mA2dpAdapterName;

        pbnjson::JValue fileJsonBluetoothConfig;
        adapterInfo mAdapterNamesMap;
        adapterDisplayIDMap mAdapterDisplayIDMap;
        adapterAddressDisplayIDMap mAdapterAddressDisplayIDMap;
        displayConnectionStatus mDisplayConnectionStatus;

        BluetoothManager (const BluetoothManager&) = delete;
        BluetoothManager& operator=(const BluetoothManager&) = delete;
        BluetoothManager(ModuleConfig* const pConfObj);
        ModuleManager* mObjModuleManager;
        AudioMixer *mObjAudioMixer;
        static bool mIsObjRegistered;
        //Register Object to object factory. This is called automatically
        static bool RegisterObject()
        {
            return (ModuleFactory::getInstance()->Register("load_bluetooth_manager", &BluetoothManager::CreateObject));
        }

    public:
        ~BluetoothManager();
        static BluetoothManager* getBluetoothManagerInstance();
        static BluetoothManager *mBluetoothManager;
        static ModuleInterface* CreateObject(ModuleConfig* const pConfObj)
        {
            if (mIsObjRegistered)
            {
                PM_LOG_DEBUG("CreateObject - Creating the BluetoothManager handler");
                mBluetoothManager = new(std::nothrow) BluetoothManager(pConfObj);
                if (mBluetoothManager)
                    return mBluetoothManager;
            }
            return nullptr;
        }
        void initialize();
        void deInitialize();
        void handleEvent(events::EVENTS_T* event);

        void eventServerStatusInfo (SERVER_TYPE_E serviceName, bool connected);
        void eventMixerStatus (bool mixerStatus, utils::EMIXER_TYPE mixerType);
        void eventKeyInfo (LUNA_KEY_TYPE_E type, LSMessage *message);

        void setBlueToothA2DPActive (bool state, const std::string &address, const std::string &profile,
                                     const std::string &adapterAddress);
        int displayOfAdapter(const std::string &adapterAddress) const;
        void btAdapterQueryInfo (LSMessage *message);
        void btDeviceGetStatusInfo (LSMessage *message);
        void btA2DPGetStatusInfo (LSMessage *message);
        void a2dpDeviceGetStatus(LSMessage *message);
        void btA2DPSourceGetStatus(LSMessage *message);
        void setBluetoothA2DPSource (bool state);

        bool readBluetoothConfigurationInfo();
        bool initializeBluetoothConfigurationInfo();
};
#endif //_BLUETOOTH_MANAGER_
