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
#include <vector>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include "audioMixer.h"
#include "moduleFactory.h"
#include "moduleManager.h"

/*
 * palmLegacyManager -- serves the Palm-era com.palm.audio API on top of audiod.
 *
 * LuneOS' UI layer is Enyo 1.0: com.palm.app.phone drives calls through TIL and
 * talks audio over com.palm.audio/phone, /ringtone, /dtmf and /telephony, the
 * enyo framework plays UI feedback through com.palm.audio/systemsounds, and
 * luna-next's device menu drives com.palm.audio/{ringtone,system}/setVolume.
 * None of that surface exists in this generation of audiod -- the scenario
 * modules that used to provide it were dropped when audiod-pro was restructured
 * around audioPolicyManager. Rather than resurrect the whole scenario engine,
 * this module reimplements the calls LuneOS actually makes and maps them onto
 * the current audiod internals.
 *
 * It also serves org.webosports.service.audio, so luna-sysmgr's DisplayManager
 * and the Settings app keep working unchanged when audio-service is retired.
 *
 * Where the numbers actually live
 * -------------------------------
 * Nothing here caches a volume of its own. That was the original sin of this
 * module: a single mVolume integer shared by every category, never pushed to
 * the mixer, which made /ringtone/setVolume and /system/setVolume the same
 * inaudible knob and forced callers to double-write the modern API to keep the
 * two views in step. Instead:
 *
 *   - per-category volume and mute (/media, /ringtone, /system, /alert, /phone,
 *     /vvm) are held by AudioPolicyManager, addressed by stream type, through
 *     its setStreamVolume()/setStreamMute() entry points;
 *   - the root category's volume is an alias of
 *     com.webos.service.audio/master, proxied over LS2 and mirrored back by a
 *     standing master/getVolume subscription.
 *
 * So a legacy caller and a modern caller are always looking at the same number.
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

        /* The call states com.palm.app.phone reports per line in
         * phone/CallStatusUpdate. Mirrors audiod-pro's own ECallStatus so the
         * Gen 2 handling could be carried over unchanged. */
        enum ECallStatus
        {
            eCallStatus_NoCall = 0,
            eCallStatus_Incoming,
            eCallStatus_Dialing,
            eCallStatus_Connecting,
            eCallStatus_Active,
            eCallStatus_OnHold,
            eCallStatus_Disconnected
        };

        /* Where audio should come out for a routable category. Named after the
         * Palm scenarios the phone app selects (phone_front_speaker & co). */
        enum EPhoneRoute
        {
            ePhoneRoute_Earpiece = 0,
            ePhoneRoute_Speaker,
            ePhoneRoute_Headset,
            ePhoneRoute_HeadsetMic,
            ePhoneRoute_BluetoothSCO,
            ePhoneRoute_A2DP,
            ePhoneRoute_Wireless
        };

        /* One Palm category and what it maps onto in this generation.
         *
         * streamType is the audiod stream that carries the category's audio, or
         * nullptr where this generation has no equivalent sink. scenarioPrefix
         * is the leading token of the category's scenario names, or nullptr for
         * categories the Palm API never made routable. */
        struct LegacyCategory
        {
            const char *category;
            const char *streamType;
            const char *scenarioPrefix;
            EPhoneRoute route;
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
         * and mAppliedRoute are what was last pushed to the hardware. Keeping
         * them separate is what makes the disconnect path safe to re-apply, and
         * what makes a mid-call speakerphone toggle actually take effect --
         * see updateCallMode(). */
        ECallMode mCallMode;
        ECallMode mAppliedCallMode;
        EPhoneRoute mAppliedRoute;
        ECallStatus mCarrierStatus;
        ECallStatus mVoipStatus;
        bool mCallWithVideo;

        bool mPhoneMuted;
        bool mMicMuted;
        bool mHac;              /* hearing aid compatibility */
        bool mVolumeLocked;
        bool mRingerOn;

        /* Master volume mirror -- see the class comment. Populated by a
         * standing subscription so the legacy view never drifts from the
         * modern one, including when something else moves the volume. */
        std::string mActiveSoundOutput;
        int mMasterVolume;
        bool mMasterMuted;

        static LegacyCategory sCategories[];

        static bool mIsObjRegistered;
        static bool RegisterObject()
        {
            return (ModuleFactory::getInstance()->Register("load_palm_legacy_manager",
                                                           &PalmLegacyManager::CreateObject));
        }

        /* Call routing is done directly against PulseAudio rather than through
         * module-palm-policy: the policy module routes streams between virtual
         * sinks, but putting a phone call through the modem needs the ALSA card
         * moved onto its voicecall profile, which only libpulse can do.
         * PulseAudioLink keeps its context private, so this module opens its
         * own -- the same thing audio-service does today. */
        pa_glib_mainloop *mPaMainloop;
        pa_context *mPaContext;
        bool mPaReady;

        /* One in-flight routing request. Heap-allocated because the PulseAudio
         * card/sink/source walk is a chain of async callbacks. mPendingCards
         * keeps the request alive across a profile switch that overlaps the
         * still-running card enumeration -- without it both the profile
         * callback and the enumeration's end-of-list would start their own sink
         * walk on the same request and both would free it. */
        struct RoutingRequest
        {
            PalmLegacyManager *self;
            ECallMode mode;
            EPhoneRoute route;
            int refs;
        };

        static RoutingRequest *routingRef(RoutingRequest *req);
        static void routingUnref(RoutingRequest *req);

        bool registerLegacyService(const char *serviceName, LSHandle **handle);
        bool registerLegacyCategories(LSHandle *handle);

        bool connectToPulse();

        static void paContextStateCb(pa_context *c, void *userdata);
        static void paCardInfoCb(pa_context *c, const pa_card_info *info, int eol, void *userdata);
        static void paCardProfileSetCb(pa_context *c, int success, void *userdata);
        static void paSinkInfoCb(pa_context *c, const pa_sink_info *info, int eol, void *userdata);
        static void paSinkPortSetCb(pa_context *c, int success, void *userdata);
        static void paSourceInfoCb(pa_context *c, const pa_source_info *info, int eol, void *userdata);
        static void paSourcePortSetCb(pa_context *c, int success, void *userdata);

        /* Push mCallMode/route to the hardware. Idempotent: returns early only
         * when neither the mode nor the route has changed since the last
         * successful application. */
        void updateCallMode();
        bool applyCallRouting(ECallMode mode, EPhoneRoute route);

        /* The combined view of the per-line states CallStatusUpdate reports. */
        ECallStatus effectiveCallStatus() const;
        void applyCallStatus();

        /* Category helpers. The Palm API is category-addressed and LS2 gives us
         * the category the message arrived on, so one handler can serve
         * /media, /ringtone, /system, /alert, /phone and /vvm alike. */
        static LegacyCategory *categoryFor(LSMessage *message);
        static LegacyCategory *categoryByName(const char *name);
        static std::vector<std::string> scenariosFor(const LegacyCategory *cat);
        static std::string scenarioName(const LegacyCategory *cat, EPhoneRoute route);
        static bool routeFromScenario(const LegacyCategory *cat, const std::string &scenario,
                                      EPhoneRoute *route);

        int categoryVolume(const LegacyCategory *cat) const;
        bool categoryMuted(const LegacyCategory *cat) const;

        /* Root category is an alias of com.webos.service.audio/master. */
        bool masterVolumeCall(const char *method, pbnjson::JValue payload);
        void subscribeMasterVolume();
        static bool _masterVolumeStatusCb(LSHandle *sh, LSMessage *reply, void *ctx);

        pbnjson::JValue rootStatus() const;
        pbnjson::JValue categoryStatus(const LegacyCategory *cat, const char *action) const;

        void notifyStatusSubscribers();
        void notifyCategory(const LegacyCategory *cat, const char *action);
        void postCategoryStatus(const std::string &key, pbnjson::JValue reply);

        LSHandle *legacyHandle() const;

        static void replyJson(LSHandle *sh, LSMessage *message, pbnjson::JValue reply);
        static void replySuccess(LSHandle *sh, LSMessage *message);

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
        static LSMethod alertMethods[];
        static LSMethod vvmMethods[];
        static LSMethod stateMethods[];
        static LSMethod systemsoundsMethods[];

        /* Root category -- the org.webosports.service.audio surface that
         * luna-sysmgr's DisplayManager and the Settings app already speak. */
        static bool _getStatus(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _rootSetVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _rootGetVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setMute(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setMicMute(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setCallMode(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _volumeUp(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _volumeDown(LSHandle *sh, LSMessage *message, void *ctx);

        /* /phone */
        static bool _phoneSetMuted(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _callStatusUpdate(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _hacSet(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _hacGet(LSHandle *sh, LSMessage *message, void *ctx);

        /* /telephony */
        static bool _telephonyAnswered(LSHandle *sh, LSMessage *message, void *ctx);

        /* /dtmf */
        static bool _playDTMF(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _stopDTMF(LSHandle *sh, LSMessage *message, void *ctx);

        /* Category-addressed: served identically for every category in
         * sCategories, dispatched on LSMessageGetCategory(). */
        static bool _status(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _getVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _offsetVolume(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setMuted(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _listScenarios(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _setCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _getCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _lockVolumeKeys(LSHandle *sh, LSMessage *message, void *ctx);
        static bool _vvmControl(LSHandle *sh, LSMessage *message, void *ctx);

        /* /state */
        static bool _setRingerSwitch(LSHandle *sh, LSMessage *message, void *ctx);

        /* /systemsounds -- enyo's UI feedback path. */
        static bool _playFeedback(LSHandle *sh, LSMessage *message, void *ctx);
};

#endif //_PALM_LEGACY_MANAGER_H_
