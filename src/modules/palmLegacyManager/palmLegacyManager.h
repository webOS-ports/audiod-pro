/* @@@LICENSE
*
* Copyright (c) 2026 LuneOS / webOS Ports
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

#ifndef _PALM_LEGACY_MANAGER_H_
#define _PALM_LEGACY_MANAGER_H_

#include <string>

#include "audioMixer.h"
#include "moduleFactory.h"
#include "moduleManager.h"

/*
 * palmLegacyManager -- serves the Palm-era com.palm.audio API on top of audiod.
 *
 * LuneOS' UI layer is Enyo 1.0: com.palm.app.phone drives calls through TIL and
 * talks audio over com.palm.audio/phone, /ringtone, /dtmf and /telephony, and
 * the enyo framework itself plays UI feedback through
 * com.palm.audio/systemsounds. None of that surface exists in this generation of
 * audiod -- the scenario modules that used to provide it were dropped when
 * audiod-pro was restructured around audioPolicyManager. Rather than resurrect
 * the whole scenario engine, this module reimplements just the calls LuneOS
 * actually makes, mapping them onto the current audiod internals.
 *
 * It also serves org.webosports.service.audio, so luna-sysmgr's DisplayManager
 * and the Settings app keep working unchanged when audio-service is retired.
 *
 * audiod's own handle (com.webos.service.audio) is registered once in main.cpp
 * and reached through GetPalmService(); LS2 gives a handle exactly one name, so
 * the two legacy names need their own handles, registered here.
 */

class PalmLegacyManager : public ModuleInterface
{
    public:
        /* Which transport a call is running over. Carrier calls route through
         * the modem's own voice path, VoIP calls stay on the normal PCM path,
         * so the two need different hardware routing. */
        enum ECallMode
        {
            eCallMode_None = 0,
            eCallMode_Carrier,
            eCallMode_Voip
        };

        /* Mirrors the status values com.palm.app.phone sends in
         * phone/CallStatusUpdate. Only Active and Disconnected change routing;
         * the rest are tracked so a scenario change during ringing can be
         * applied once the call goes active. */
        enum ECallStatus
        {
            eCallStatus_None = 0,
            eCallStatus_Incoming,
            eCallStatus_Dialing,
            eCallStatus_Active,
            eCallStatus_Disconnected
        };

        /* Where phone audio should come out. Named after the Palm scenarios the
         * phone app selects (phone_front_speaker and friends). */
        enum EPhoneRoute
        {
            ePhoneRoute_Earpiece = 0,
            ePhoneRoute_Speaker,
            ePhoneRoute_Headset,
            ePhoneRoute_BluetoothSCO
        };

    private:
        PalmLegacyManager(const PalmLegacyManager&) = delete;
        PalmLegacyManager& operator=(const PalmLegacyManager&) = delete;
        PalmLegacyManager(ModuleConfig* const pConfObj);

        AudioMixer *mObjAudioMixer;
        ModuleManager *mObjModuleManager;

        /* One LS2 handle per legacy bus name. Both carry the same categories,
         * so callers may use either name interchangeably. */
        LSHandle *mPalmHandle;      /* com.palm.audio */
        LSHandle *mPortsHandle;     /* org.webosports.service.audio */

        /* Call state. mCallMode is what the phone app told us; mAppliedCallMode
         * is what was last pushed to the hardware. Keeping them separate is what
         * makes the disconnect path safe to re-apply -- see updateCallMode(). */
        ECallMode mCallMode;
        ECallMode mAppliedCallMode;
        ECallStatus mCallStatus;

        EPhoneRoute mPhoneRoute;
        bool mSpeakerMode;
        bool mPhoneMuted;
        bool mRingtoneMuted;
        bool mMicMuted;
        bool mHac;              /* hearing aid compatibility */
        bool mVolumeLocked;

        int mVolume;
        bool mMuted;

        static bool mIsObjRegistered;
        static bool RegisterObject()
        {
            return (ModuleFactory::getInstance()->Register("load_palm_legacy_manager",
                                                           &PalmLegacyManager::CreateObject));
        }

        bool registerLegacyService(const char *serviceName, LSHandle **handle);
        bool registerLegacyCategories(LSHandle *handle);

        /* Push mCallMode to the hardware. Idempotent: returns early when the
         * mode has not changed since the last successful application. */
        void updateCallMode();

        /* Phase 4 fills this in with the real PulseAudio card-profile and port
         * switching. Kept separate from updateCallMode() so the state machine
         * can be reasoned about (and fixed) independently of the routing. */
        bool applyCallRouting(ECallMode mode, EPhoneRoute route);

        void notifyStatusSubscribers();
        void postCategoryStatus(const char *category, const char *method,
                                pbnjson::JValue reply);

    public:
        ~PalmLegacyManager();

        static PalmLegacyManager* getPalmLegacyManagerInstance();
        static PalmLegacyManager* mPalmLegacyManager;

        static ModuleInterface* CreateObject(ModuleConfig* const pConfObj)
        {
            if (mIsObjRegistered)
            {
                PM_LOG_DEBUG("CreateObject - Creating the PalmLegacyManager handler");
                mPalmLegacyManager = new(std::nothrow) PalmLegacyManager(pConfObj);
                if (mPalmLegacyManager)
                    return mPalmLegacyManager;
            }
            return nullptr;
        }

        void initialize();
        void deInitialize();
        void handleEvent(events::EVENTS_T* ev);

        /* Category method tables. */
        static LSMethod rootMethods[];
        static LSMethod phoneMethods[];
        static LSMethod ringtoneMethods[];
        static LSMethod telephonyMethods[];
        static LSMethod dtmfMethods[];
        static LSMethod mediaMethods[];
        static LSMethod systemMethods[];
        static LSMethod vvmMethods[];
        static LSMethod systemsoundsMethods[];

        /* Root category -- the org.webosports.service.audio surface that
         * luna-sysmgr's DisplayManager and the Settings app already speak. */
        static bool _getStatus(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setMute(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setMicMute(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setCallMode(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _volumeUp(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _volumeDown(LSHandle *sh, LSMessage *message, void *ctx);

        /* /phone */
        static bool _phoneStatus(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _phoneSetMuted(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _callStatusUpdate(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _hacSet(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _hacGet(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _getCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx);

        /* /ringtone */
        static bool _ringtoneSetMuted(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _ringtoneStatus(LSHandle *sh, LSMessage *message, void *ctx);

        /* /telephony */
        static bool _telephonyAnswered(LSHandle *sh, LSMessage *message, void *ctx);

        /* /dtmf */
        static bool _playDTMF(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _stopDTMF(LSHandle *sh, LSMessage *message, void *ctx);

        /* Shared by /media, /system and /vvm, which differ only in which
         * stream they nominally address. */
        static bool _genericStatus(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _genericSetVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _genericGetVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _lockVolumeKeys(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _vvmControl(LSHandle *sh, LSMessage *message, void *ctx);

        /* /systemsounds -- enyo's UI feedback path. */
        static bool _playFeedback(LSHandle *sh, LSMessage *message, void *ctx);
};

#endif //_PALM_LEGACY_MANAGER_H_
