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

#include <unistd.h>
#include <strings.h>

#include <initializer_list>
#include <utility>

#include "palmLegacyManager.h"
#include "settingsRequest.h"
#include "legacyRequest.h"
#include "audioPolicyManager.h"
#include "audioRouter.h"
#include "main.h"

#define PALM_AUDIO_SERVICE      "com.palm.audio"
#define PORTS_AUDIO_SERVICE     "org.webosports.service.audio"

#define MASTER_GET_VOLUME       "luna://com.webos.service.audio/master/getVolume"
#define MASTER_URI_PREFIX       "luna://com.webos.service.audio/master/"
/* Where audiod keeps its settings: as OSEMasterVolumeManager does. */
#define SETTINGS_GET            "luna://com.webos.service.settings/getSystemSettings"
#define SETTINGS_SET            "luna://com.webos.service.settings/setSystemSettings"
#define SETTINGS_CATEGORY       "sound"
#define KEY_ROOT_SETTINGS       "/getSettings"

#define MEDIA_SESSIONS_URI      "luna://com.webos.service.mediacontroller/getActiveMediaSessions"
#define MEDIA_PLAY_STATUS_URI   "luna://com.webos.service.mediacontroller/getMediaPlayStatus"
#define MEDIA_KEY_URI           "luna://com.webos.service.mediacontroller/injectMediaKeyEvent"

/* Subscription keys. LSSubscriptionReply matches on an opaque string, so these
 * only have to agree with what the corresponding handler passes to
 * LSSubscriptionProcess -- which is "<category>/<method>". */
#define KEY_ROOT_STATUS         "/getStatus"

bool PalmLegacyManager::mIsObjRegistered = PalmLegacyManager::RegisterObject();
PalmLegacyManager* PalmLegacyManager::mPalmLegacyManager = nullptr;

/* The Palm categories this module serves, and what each maps onto here.
 *
 * streamType is the audiod stream that actually carries the category's audio.
 * This generation's virtual-sink enum is the TV-oriented one, so there is no
 * ealarm/etimer/enotifications/evvm/enavigation and hence no /alarm, /timer,
 * /notification or /nav: a category with nothing behind it would be a lie.
 * /vvm shares pmedia because voicemail playback goes out the media path here.
 *
 * scenarioPrefix is the leading token of the category's Palm scenario names,
 * or nullptr for the categories the Palm API never made routable. */
PalmLegacyManager::LegacyCategory PalmLegacyManager::sCategories[] = {
    { "/media",     "pmedia",     "media", ePhoneRoute_Speaker  },
    { "/ringtone",  "pringtones", nullptr, ePhoneRoute_Speaker  },
    { "/system",    "pfeedback",  nullptr, ePhoneRoute_Speaker  },
    { "/alert",     "palerts",    nullptr, ePhoneRoute_Speaker  },
    { "/phone",     "voipcall",   "phone", ePhoneRoute_Earpiece },
    { "/vvm",       "pmedia",     "vvm",   ePhoneRoute_Earpiece },
    { nullptr,      nullptr,      nullptr, ePhoneRoute_Earpiece },
};

PalmLegacyManager* PalmLegacyManager::getPalmLegacyManagerInstance()
{
    return mPalmLegacyManager;
}

PalmLegacyManager::PalmLegacyManager(ModuleConfig* const pConfObj) :
    mObjAudioMixer(AudioMixer::getAudioMixerInstance()),
    mObjModuleManager(ModuleManager::getModuleManagerInstance()),
    mPalmHandle(nullptr),
    mPortsHandle(nullptr),
    mCallMode(eCallMode_None),
    mAppliedCallMode(eCallMode_None),
    mAppliedRoute(ePhoneRoute_Earpiece),
    mAppliedRing(false),
    mPhoneMuted(false),
    mMicMuted(false),
    mHac(false),
    mVolumeLocked(false),
    mRingerOn(true),
    mActiveSoundOutput(""),
    mMasterVolume(0),
    mMasterMuted(false),
    mJackState(eJack_Unknown),
    mJackSeenUnplugged(false),
    mBtAudioSink(""),
    mBtAudioName(""),
    mUsbAudioSink(""),
    mHfpAgent(nullptr),
    mHeadsetTakesCall(true),
    mAlertsOnSpeaker(false),
    mRingtoneVolume(100),   // the policy config's currentVolume for pringtones
    mRingtoneMuted(false),
    mResumeMediaAfterCall(false),
    mMediaGeneration(0),
    mResumeMediaTimer(0),
    mPaMainloop(nullptr),
    mPaContext(nullptr),
    mPaReady(false),
    mPaReconnectTimer(0)
{
    if (!mObjAudioMixer)
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "AudioMixer instance is null");
    PM_LOG_DEBUG("palmLegacyManager constructor");
}

PalmLegacyManager::~PalmLegacyManager()
{
    PM_LOG_DEBUG("palmLegacyManager destructor");
    delete mHfpAgent;
}

/* ------------------------------------------------------------------------- *
 * Small reply helpers
 * ------------------------------------------------------------------------- */

void PalmLegacyManager::replyJson(LSHandle *sh, LSMessage *message, pbnjson::JValue reply)
{
    CLSError lserror;
    std::string payload = reply.stringify();
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
}

void PalmLegacyManager::replySuccess(LSHandle *sh, LSMessage *message)
{
    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
}

/* A payload that passed its schema but lacks what the method needs. */
static void replyInvalid(LSHandle *sh, LSMessage *message, const char *errorText)
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", false);
    reply.put("errorCode", AUDIOD_ERRORCODE_INVALID_PARAMS);
    reply.put("errorText", errorText);
    CLSError lserror;
    if (!LSMessageReply(sh, message, reply.stringify().c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
}

/* Outbound calls to the Palm-era services (com.palm.telephony and friends) go
 * out under a legacy bus name: those are the identities whose LS2 role grants
 * outbound access to them. */
LSHandle *PalmLegacyManager::legacyHandle() const
{
    if (mPalmHandle)
        return mPalmHandle;
    if (mPortsHandle)
        return mPortsHandle;
    return GetPalmService();
}

/* ------------------------------------------------------------------------- *
 * Method tables
 * ------------------------------------------------------------------------- */

LSMethod PalmLegacyManager::rootMethods[] = {
    { "getStatus",      PalmLegacyManager::_getStatus },
    { "getSettings",    PalmLegacyManager::_getSettings, LUNA_METHOD_FLAGS_NONE },
    { "setSettings",    PalmLegacyManager::_setSettings, LUNA_METHOD_FLAGS_NONE },
    { "status",         PalmLegacyManager::_getStatus },
    { "setVolume",      PalmLegacyManager::_rootSetVolume },
    { "getVolume",      PalmLegacyManager::_rootGetVolume },
    { "setMute",        PalmLegacyManager::_setMute },
    { "setMuted",       PalmLegacyManager::_setMute },
    { "setMicMute",     PalmLegacyManager::_setMicMute },
    { "setCallMode",    PalmLegacyManager::_setCallMode },
    { "volumeUp",       PalmLegacyManager::_volumeUp },
    { "volumeDown",     PalmLegacyManager::_volumeDown },
    { "playFeedback",   PalmLegacyManager::_playFeedback },
    { },
};

LSMethod PalmLegacyManager::phoneMethods[] = {
    { "enableScenario",     PalmLegacyManager::_enableScenario },
    { "disableScenario",    PalmLegacyManager::_disableScenario },
    { "status",                 PalmLegacyManager::_status },
    { "setMuted",               PalmLegacyManager::_phoneSetMuted },
    { "CallStatusUpdate",       PalmLegacyManager::_callStatusUpdate },
    { "hacSet",                 PalmLegacyManager::_hacSet },
    { "hacGet",                 PalmLegacyManager::_hacGet },
    { "setCurrentScenario",     PalmLegacyManager::_setCurrentScenario },
    { "getCurrentScenario",     PalmLegacyManager::_getCurrentScenario },
    { "listScenarios",          PalmLegacyManager::_listScenarios },
    { "setVolume",              PalmLegacyManager::_setVolume },
    { "getVolume",              PalmLegacyManager::_getVolume },
    { "offsetVolume",           PalmLegacyManager::_offsetVolume },
    { "lockVolumeKeys",         PalmLegacyManager::_lockVolumeKeys },
    { },
};

LSMethod PalmLegacyManager::ringtoneMethods[] = {
    { "status",         PalmLegacyManager::_status },
    { "setMuted",       PalmLegacyManager::_setMuted },
    { "setVolume",      PalmLegacyManager::_setVolume },
    { "getVolume",      PalmLegacyManager::_getVolume },
    { "offsetVolume",   PalmLegacyManager::_offsetVolume },
    { "lockVolumeKeys", PalmLegacyManager::_lockVolumeKeys },
    { },
};

LSMethod PalmLegacyManager::telephonyMethods[] = {
    { "answered",   PalmLegacyManager::_telephonyAnswered },
    { },
};

LSMethod PalmLegacyManager::dtmfMethods[] = {
    { "playDTMF",   PalmLegacyManager::_playDTMF },
    { "stopDTMF",   PalmLegacyManager::_stopDTMF },
    { },
};

LSMethod PalmLegacyManager::mediaMethods[] = {
    { "enableScenario",     PalmLegacyManager::_enableScenario },
    { "disableScenario",    PalmLegacyManager::_disableScenario },
    { "status",             PalmLegacyManager::_status },
    { "setVolume",          PalmLegacyManager::_setVolume },
    { "getVolume",          PalmLegacyManager::_getVolume },
    { "offsetVolume",       PalmLegacyManager::_offsetVolume },
    { "setMuted",           PalmLegacyManager::_setMuted },
    { "setCurrentScenario", PalmLegacyManager::_setCurrentScenario },
    { "getCurrentScenario", PalmLegacyManager::_getCurrentScenario },
    { "listScenarios",      PalmLegacyManager::_listScenarios },
    { "lockVolumeKeys",     PalmLegacyManager::_lockVolumeKeys },
    { },
};

LSMethod PalmLegacyManager::systemMethods[] = {
    { "status",         PalmLegacyManager::_status },
    { "setVolume",      PalmLegacyManager::_setVolume },
    { "getVolume",      PalmLegacyManager::_getVolume },
    { "offsetVolume",   PalmLegacyManager::_offsetVolume },
    { "setMuted",       PalmLegacyManager::_setMuted },
    { },
};

LSMethod PalmLegacyManager::alertMethods[] = {
    { "status",         PalmLegacyManager::_status },
    { "setVolume",      PalmLegacyManager::_setVolume },
    { "getVolume",      PalmLegacyManager::_getVolume },
    { "offsetVolume",   PalmLegacyManager::_offsetVolume },
    { "setMuted",       PalmLegacyManager::_setMuted },
    { },
};

LSMethod PalmLegacyManager::vvmMethods[] = {
    { "enableScenario",     PalmLegacyManager::_enableScenario },
    { "disableScenario",    PalmLegacyManager::_disableScenario },
    { "status",             PalmLegacyManager::_status },
    { "control",            PalmLegacyManager::_vvmControl },
    { "setVolume",          PalmLegacyManager::_setVolume },
    { "getVolume",          PalmLegacyManager::_getVolume },
    { "offsetVolume",       PalmLegacyManager::_offsetVolume },
    { "setCurrentScenario", PalmLegacyManager::_setCurrentScenario },
    { "getCurrentScenario", PalmLegacyManager::_getCurrentScenario },
    { "listScenarios",      PalmLegacyManager::_listScenarios },
    { },
};

LSMethod PalmLegacyManager::stateMethods[] = {
    { "setRingerSwitch",    PalmLegacyManager::_setRingerSwitch },
    { },
};

LSMethod PalmLegacyManager::systemsoundsMethods[] = {
    { "playFeedback",   PalmLegacyManager::_playFeedback },
    { },
};

/* ------------------------------------------------------------------------- *
 * Category helpers
 * ------------------------------------------------------------------------- */

PalmLegacyManager::LegacyCategory *PalmLegacyManager::categoryByName(const char *name)
{
    if (!name)
        return nullptr;
    for (LegacyCategory *c = sCategories; c->category; ++c)
    {
        if (0 == strcmp(c->category, name))
            return c;
    }
    return nullptr;
}

PalmLegacyManager::LegacyCategory *PalmLegacyManager::categoryFor(LSMessage *message)
{
    return categoryByName(message ? LSMessageGetCategory(message) : nullptr);
}

/* The Palm scenario names for a category, in the order the phone app expects
 * to list them. Only routes that exist on a handset are offered: this is the
 * list com.palm.app.phone builds its audio-route picker from. */
std::vector<std::string> PalmLegacyManager::scenariosFor(const LegacyCategory *cat) const
{
    std::vector<std::string> list;
    if (!cat || !cat->scenarioPrefix)
        return list;

    const std::string p(cat->scenarioPrefix);
    std::vector<std::string> all;
    all.push_back(p + "_front_speaker");
    all.push_back(p + "_back_speaker");
    all.push_back(p + "_headset");
    all.push_back(p + "_headset_mic");
    if (0 == strcmp(cat->scenarioPrefix, "media"))
    {
        all.push_back(p + "_a2dp");
        all.push_back(p + "_wireless");
    }
    else
    {
        all.push_back(p + "_bluetooth_sco");
    }

    /* A disabled scenario is not offered. This is how HAC removes the
     * speakerphone, and what disableScenario meant. */
    for (const auto &name : all)
    {
        if (mDisabledScenarios.find(name) == mDisabledScenarios.end())
            list.push_back(name);
    }
    return list;
}

std::string PalmLegacyManager::scenarioName(const LegacyCategory *cat, EPhoneRoute route)
{
    if (!cat || !cat->scenarioPrefix)
        return std::string();

    const std::string p(cat->scenarioPrefix);
    switch (route)
    {
        case ePhoneRoute_Speaker:       return p + "_back_speaker";
        case ePhoneRoute_Headset:       return p + "_headset";
        case ePhoneRoute_HeadsetMic:    return p + "_headset_mic";
        case ePhoneRoute_BluetoothSCO:  return p + "_bluetooth_sco";
        case ePhoneRoute_A2DP:          return p + "_a2dp";
        case ePhoneRoute_Wireless:      return p + "_wireless";
        case ePhoneRoute_Earpiece:
        default:                        return p + "_front_speaker";
    }
}

bool PalmLegacyManager::routeFromScenario(const LegacyCategory *cat, const std::string &scenario,
                                          EPhoneRoute *route)
{
    if (!cat || !cat->scenarioPrefix || !route)
        return false;

    const std::string p = std::string(cat->scenarioPrefix) + "_";
    if (scenario.compare(0, p.size(), p) != 0)
        return false;

    const std::string suffix = scenario.substr(p.size());
    if (suffix == "front_speaker")      *route = ePhoneRoute_Earpiece;
    else if (suffix == "back_speaker")  *route = ePhoneRoute_Speaker;
    else if (suffix == "headset")       *route = ePhoneRoute_Headset;
    else if (suffix == "headset_mic")   *route = ePhoneRoute_HeadsetMic;
    else if (suffix == "bluetooth_sco") *route = ePhoneRoute_BluetoothSCO;
    else if (suffix == "a2dp")          *route = ePhoneRoute_A2DP;
    else if (suffix == "wireless")      *route = ePhoneRoute_Wireless;
    else return false;

    return true;
}

void PalmLegacyManager::applyHac()
{
    LegacyCategory *phone = categoryByName("/phone");
    if (!phone)
        return;

    const std::string backSpeaker = scenarioName(phone, ePhoneRoute_Speaker);

    if (mHac)
    {
        /* Take the speakerphone out of service and put the call on the
         * earpiece, unconditionally -- the same two steps webOS 3.0.5 took. */
        mDisabledScenarios.insert(backSpeaker);
        phone->route = ePhoneRoute_Earpiece;
        updateCallMode();
    }
    else
    {
        mDisabledScenarios.erase(backSpeaker);
    }

    notifyCategory(phone, "changed");
}

int PalmLegacyManager::categoryVolume(const LegacyCategory *cat) const
{
    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    if (!cat || !cat->streamType || !policy)
        return 0;

    const int volume = policy->getStreamVolume(cat->streamType);
    return (volume < 0) ? 0 : volume;
}

bool PalmLegacyManager::categoryMuted(const LegacyCategory *cat) const
{
    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    if (!cat || !cat->streamType || !policy)
        return false;
    return policy->getStreamMute(cat->streamType);
}

/* ------------------------------------------------------------------------- *
 * Service registration
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::registerLegacyService(const char *serviceName, LSHandle **handle)
{
    CLSError lserror;

    if (!LSRegister(serviceName, handle, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to register legacy service name %s", serviceName);
        *handle = nullptr;
        return false;
    }

    if (!registerLegacyCategories(*handle))
        return false;

    /* audiod runs its own GMainContext; attach to that rather than the default
     * one, or the handle never pumps. */
    if (!LSGmainContextAttach(*handle, GetMainLoopContext(), &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to attach %s to the audiod main loop", serviceName);
        return false;
    }

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Serving legacy audio API as %s", serviceName);
    return true;
}

bool PalmLegacyManager::registerLegacyCategories(LSHandle *handle)
{
    CLSError lserror;

    struct { const char *category; LSMethod *methods; } categories[] = {
        { "/",              rootMethods },
        { "/phone",         phoneMethods },
        { "/ringtone",      ringtoneMethods },
        { "/telephony",     telephonyMethods },
        { "/dtmf",          dtmfMethods },
        { "/media",         mediaMethods },
        { "/system",        systemMethods },
        { "/alert",         alertMethods },
        { "/vvm",           vvmMethods },
        { "/state",         stateMethods },
        { "/systemsounds",  systemsoundsMethods },
    };

    for (auto &entry : categories)
    {
        if (!LSRegisterCategory(handle, entry.category, entry.methods, nullptr, nullptr, &lserror))
        {
            lserror.Print(__FUNCTION__, __LINE__);
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Registering category %s failed", entry.category);
            return false;
        }

        if (!LSCategorySetData(handle, entry.category, this, &lserror))
        {
            lserror.Print(__FUNCTION__, __LINE__);
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Setting category data for %s failed", entry.category);
            return false;
        }
    }

    return true;
}

void PalmLegacyManager::initialize()
{
    if (!mPalmLegacyManager)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "mPalmLegacyManager is nullptr");
        return;
    }

    /* Both names are registered independently: if one is already taken -- most
     * likely because audio-service is still installed -- the other should still
     * come up rather than taking the whole module down with it. */
    registerLegacyService(PALM_AUDIO_SERVICE, &mPalmHandle);
    registerLegacyService(PORTS_AUDIO_SERVICE, &mPortsHandle);

    /* Connected with PA_CONTEXT_NOFAIL so audiod starting before PulseAudio is
     * not fatal; paContextStateCb re-applies any live call once it is up. */
    if (!connectToPulse())
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not open a PulseAudio connection; call routing is unavailable");

    /* Calls on a hands-free headset: be oFono's audio agent, so its
     * gateway hands the SCO links to audiod, which routes the call. */
    mHfpAgent = new(std::nothrow) HfpAgent(*this, [](bool error, const std::string &message) {
        if (error)
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "%s", message.c_str());
        else
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "%s", message.c_str());
    });
    if (!mHfpAgent || !mHfpAgent->start())
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not start the hands-free audio agent; calls stay off Bluetooth headsets");

    /* Mirror the master volume so the root category can serve it without
     * caching a number of its own. */
    subscribeMasterVolume();

    /* The active output device is what master/setVolume has to be addressed
     * at, and audioRouter is the thing that knows it. */
    if (mObjModuleManager)
    {
        mObjModuleManager->subscribeModuleEvent(this, utils::eEventActiveDeviceInfo);
        /* PulseAudio coming back: see tellRouterJack(). */
        mObjModuleManager->subscribeModuleEvent(this, utils::eEventMixerStatus);
        /* settingsservice coming up: the stored settings can be read. */
        mObjModuleManager->subscribeServerStatusInfo(this, eSettingsService2);
    }

    if (!mPalmHandle && !mPortsHandle)
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Neither legacy audio service name could be registered; the Palm-era "
            "API is unavailable. Is audio-service still installed?");
    else
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Successfully initialized PalmLegacyManager");
}

void PalmLegacyManager::deInitialize()
{
    CLSError lserror;

    if (mPalmHandle && !LSUnregister(mPalmHandle, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    if (mPortsHandle && !LSUnregister(mPortsHandle, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    mPalmHandle = nullptr;
    mPortsHandle = nullptr;

    cancelResumeMedia();
    /* A ringtone volume not stored yet is stored now. */
    if (mStoreRingtoneTimer)
    {
        g_source_remove(mStoreRingtoneTimer);
        mStoreRingtoneTimer = 0;
        writeRingtoneVolume();
    }
    if (mPaReconnectTimer)
    {
        g_source_remove(mPaReconnectTimer);
        mPaReconnectTimer = 0;
    }
    dropPaContext();
    if (mPaMainloop)
    {
        pa_glib_mainloop_free(mPaMainloop);
        mPaMainloop = nullptr;
    }
    mPaReady = false;

    if (mHfpAgent)
    {
        mHfpAgent->stop();
        delete mHfpAgent;
        mHfpAgent = nullptr;
    }

    if (mPalmLegacyManager)
    {
        delete mPalmLegacyManager;
        mPalmLegacyManager = nullptr;
    }
}

void PalmLegacyManager::handleEvent(events::EVENTS_T* ev)
{
    if (!ev)
        return;

    switch ((int)ev->eventName)
    {
        case utils::eEventActiveDeviceInfo:
        {
            events::EVENT_ACTIVE_DEVICE_INFO_T *info =
                (events::EVENT_ACTIVE_DEVICE_INFO_T*) ev;
            if (info->isOutput && info->isActive)
            {
                PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                    "Active sound output is now %s", info->deviceName.c_str());
                mActiveSoundOutput = info->deviceName;
                setActiveOutput(info->deviceName);
            }
        }
        break;
        case utils::eEventMixerStatus:
        {
            const events::EVENT_MIXER_STATUS_T *status =
                (const events::EVENT_MIXER_STATUS_T*) ev;
            /* audioRouter marked every output disconnected when PulseAudio
             * went away, the headphones too. PulseAudio announces its sinks
             * again, but the jack is no sink of its own, and its state only
             * reaches the router from here, on a change: tell it again. */
            if (status->mixerStatus && status->mixerType == utils::ePulseMixer && jackPlugged())
                tellRouterJack();
        }
        break;
        case utils::eEventServerStatusSubscription:
        {
            const events::EVENT_SERVER_STATUS_INFO_T *status =
                (const events::EVENT_SERVER_STATUS_INFO_T*) ev;
            if (status->serviceName == eSettingsService2 && status->connectionStatus)
                readStoredSettings();
        }
        break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------------- *
 * Master volume -- the root category is an alias of
 * com.webos.service.audio/master
 *
 * Proxying rather than reimplementing is deliberate: master volume already
 * knows the per-device volume map and persists it through settingsservice, so
 * going through it is what makes the legacy numbers survive a reboot and stay
 * identical to what the modern API reports.
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_masterVolumeStatusCb(LSHandle *sh, LSMessage *reply, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    if (!self || !reply)
        return true;

    const char *payload = LSMessageGetPayload(reply);
    if (!payload)
        return true;

    pbnjson::JValue obj = pbnjson::JDomParser::fromString(payload);
    if (!obj.isObject())
        return true;

    bool changed = false;
    int volume = 0;
    bool muted = false;
    std::string soundOutput;

    if (obj["volume"].asNumber(volume) == CONV_OK && volume != self->mMasterVolume)
    {
        self->mMasterVolume = volume;
        changed = true;
    }
    if (obj["muted"].asBool(muted) == CONV_OK && muted != self->mMasterMuted)
    {
        self->mMasterMuted = muted;
        changed = true;
    }
    if (obj["soundOutput"].asString(soundOutput) == CONV_OK && !soundOutput.empty())
        self->mActiveSoundOutput = soundOutput;

    /* Anything that moves the master volume -- the device menu's Media slider,
     * the hardware keys, another app -- lands here, so legacy subscribers see
     * it too. This is the half that was missing before: handleEvent() was
     * empty and the legacy view could not observe the modern one. */
    if (changed)
        self->notifyStatusSubscribers();

    return true;
}

void PalmLegacyManager::subscribeMasterVolume()
{
    CLSError lserror;
    LSHandle *sh = GetPalmService();
    if (!sh)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "subscribeMasterVolume: audiod's own service handle is null");
        return;
    }

    if (!LSCall(sh, MASTER_GET_VOLUME, "{\"subscribe\":true}",
                &PalmLegacyManager::_masterVolumeStatusCb, this, nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not subscribe to master/getVolume; the root category will "
            "report a stale volume");
    }
}

bool PalmLegacyManager::masterVolumeCall(const char *method, pbnjson::JValue payload)
{
    CLSError lserror;
    LSHandle *sh = GetPalmService();
    if (!sh || !method)
        return false;

    if (mActiveSoundOutput.empty())
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "masterVolumeCall(%s): no active sound output known yet", method);
        return false;
    }

    /* the master methods require soundOutput; the Palm API never carried one, which is
     * exactly why callers had to double-write both APIs to keep them in step. */
    payload.put("soundOutput", mActiveSoundOutput);

    const std::string uri = std::string(MASTER_URI_PREFIX) + method;
    const std::string body = payload.stringify();

    if (!LSCall(sh, uri.c_str(), body.c_str(), nullptr, nullptr, nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "masterVolumeCall: %s failed", uri.c_str());
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- *
 * Call state
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::connectToPulse()
{
    mPaMainloop = pa_glib_mainloop_new(GetMainLoopContext());
    if (!mPaMainloop)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not create a PulseAudio glib mainloop");
        return false;
    }

    if (!openPaContext())
        schedulePaReconnect();

    return true;
}

bool PalmLegacyManager::openPaContext()
{
    char name[64];

    /* Cannot truncate: 64 bytes for a fixed prefix plus a pid. */
    (void) snprintf(name, sizeof(name), "PalmLegacyManager:%i", getpid());
    mPaContext = pa_context_new(pa_glib_mainloop_get_api(mPaMainloop), name);
    if (!mPaContext)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not create a PulseAudio context");
        return false;
    }

    pa_context_set_state_callback(mPaContext, &PalmLegacyManager::paContextStateCb, this);

    if (pa_context_connect(mPaContext, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not connect to PulseAudio: %s",
            pa_strerror(pa_context_errno(mPaContext)));
        dropPaContext();
        return false;
    }

    return true;
}

void PalmLegacyManager::dropPaContext()
{
    if (!mPaContext)
        return;

    /* Callbacks cleared first: disconnecting reports TERMINATED, which would
     * schedule yet another connection. */
    pa_context_set_state_callback(mPaContext, nullptr, nullptr);
    pa_context_set_subscribe_callback(mPaContext, nullptr, nullptr);
    pa_context_disconnect(mPaContext);
    pa_context_unref(mPaContext);
    mPaContext = nullptr;
    mPaReady = false;
    /* Its operations went with it, callbacks and all. */
    mUsbRingDrops.clear();
    mUsbRing.paGone();
}

void PalmLegacyManager::schedulePaReconnect()
{
    if (mPaReconnectTimer)
        return;

    const unsigned delayMs = mPaReconnect.lost();
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Opening a new PulseAudio connection in %u ms", delayMs);
    mPaReconnectTimer = g_timeout_add(delayMs, &PalmLegacyManager::paReconnectCb, this);
}

gboolean PalmLegacyManager::paReconnectCb(gpointer userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);

    self->mPaReconnectTimer = 0;
    /* The dead context goes here rather than in its own state callback. */
    self->dropPaContext();
    if (!self->openPaContext())
        self->schedulePaReconnect();

    return G_SOURCE_REMOVE;
}

void PalmLegacyManager::paContextStateCb(pa_context *c, void *userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self)
        return;

    switch (pa_context_get_state(c))
    {
        case PA_CONTEXT_READY:
            self->mPaReady = true;
            self->mPaReconnect.connected();
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Connected to PulseAudio for call routing");
            /* Assert routing for our current state so the hardware matches
             * audiod's view whenever we (re)connect. A restarted PulseAudio
             * comes back with the card on its default profile, so a call in
             * progress gets its voicecall profile and ports back here, and the
             * sink walk below reports the jack again. PulseAudio outlives an
             * audiod restart and keeps whatever card profile / sink port the
             * previous instance left -- which can be a bare 'voicecall' profile
             * or a parked output port, giving no sound. Applying even the idle
             * (eCallMode_None) routing here restores the default profile and a
             * real output port (speaker/earpiece); it is idempotent, so it is a
             * no-op when the hardware is already correct. This also re-pushes an
             * in-progress call's routing if PulseAudio came up mid-call. */
            if (self->applyCallRouting(self->mCallMode, self->effectiveRoute()))
            {
                self->mAppliedCallMode = self->mCallMode;
                self->mAppliedRoute = self->effectiveRoute();
                self->mAppliedRing = self->mRingState.ringing();
            }

            /* Follow the jack from here on. Port availability changes arrive
             * as sink and source change events; the initial walk picks up
             * whatever is plugged in already, including at boot. Sink-input
             * events say when a ringtone or alarm starts and stops. */
            pa_context_set_subscribe_callback(c, &PalmLegacyManager::paSubscribeCb, self);
            {
                pa_operation *op = pa_context_subscribe(c, (pa_subscription_mask_t)
                                                        (PA_SUBSCRIPTION_MASK_SINK |
                                                         PA_SUBSCRIPTION_MASK_SINK_INPUT),
                                                        nullptr, nullptr);
                if (op)
                    pa_operation_unref(op);
                else
                    PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                        "Could not subscribe to PulseAudio sink events; headphones "
                        "will not be followed");

                op = pa_context_get_sink_info_list(c, &PalmLegacyManager::paJackSinkCb, self);
                if (op)
                    pa_operation_unref(op);

                op = pa_context_get_sink_input_info_list(c, &PalmLegacyManager::paRingInputCb, self);
                if (op)
                    pa_operation_unref(op);

                /* A combined loudspeaker-and-USB sink an earlier audiod left
                 * goes first; then the one for now can be loaded. */
                op = pa_context_get_sink_info_by_name(c, UsbRing::SINK,
                                                      &PalmLegacyManager::paUsbRingStaleCb, self);
                if (op)
                    pa_operation_unref(op);
            }
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            /* The context is dead for good: PA_CONTEXT_NOFAIL only covers the
             * first connect, so a new one is opened once PulseAudio is back,
             * and its READY re-applies the call mode, ports and jack. Drop the
             * applied mode so the next transition is re-pushed to the hardware
             * rather than being swallowed by updateCallMode()'s early return.
             * The jack state stays: it is the last thing known, and the new
             * connection's sink walk corrects it. */
            self->mPaReady = false;
            self->mAppliedCallMode = eCallMode_None;
            self->mRingState.clear();
            /* The combined sink went with PulseAudio: the router must not
             * send the ringtones to it once PulseAudio is back. */
            self->mUsbRing.paGone();
            self->syncUsbRing();
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Lost the PulseAudio connection; call routing is suspended");
            self->schedulePaReconnect();
            break;
        default:
            break;
    }
}

/* The routing request is refcounted because a card that needs a profile change
 * spawns a continuation (paCardProfileSetCb) while the card enumeration that
 * spawned it is still running and will deliver its own end-of-list. Both then
 * walk the sinks and both would reach the single delete at the end of the
 * source walk. Counting the chains instead is what makes that safe. */
PalmLegacyManager::RoutingRequest *PalmLegacyManager::routingRef(RoutingRequest *req)
{
    if (req)
        ++req->refs;
    return req;
}

void PalmLegacyManager::routingUnref(RoutingRequest *req)
{
    if (req && --req->refs <= 0)
        delete req;
}

bool PalmLegacyManager::applyCallRouting(ECallMode mode, EPhoneRoute route)
{
    if (!mPaReady || !mPaContext)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "applyCallRouting: PulseAudio is not ready, deferring");
        return false;
    }

    RoutingRequest *req = new(std::nothrow) RoutingRequest{this, mode, route,
                                                           mRingState.ringing(), 1};
    if (!req)
        return false;

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "applyCallRouting: mode=%d route=%d ring=%d", (int) mode, (int) route, (int) req->ring);

    /* Cards first: on a carrier call the codec has to be on its voicecall
     * profile before the sink and source ports below even exist. */
    pa_operation *op = pa_context_get_card_info_list(mPaContext,
                                                     &PalmLegacyManager::paCardInfoCb, req);
    if (!op)
    {
        routingUnref(req);
        return false;
    }
    pa_operation_unref(op);
    return true;
}

void PalmLegacyManager::paCardInfoCb(pa_context *c, const pa_card_info *info, int eol, void *userdata)
{
    RoutingRequest *req = static_cast<RoutingRequest*>(userdata);
    if (!req)
        return;

    if (eol)
    {
        /* End of the card enumeration: hand this chain's reference on to the
         * sink walk. */
        pa_operation *op = pa_context_get_sink_info_list(c, &PalmLegacyManager::paSinkInfoCb, req);
        if (op)
            pa_operation_unref(op);
        else
            routingUnref(req);
        return;
    }

    if (!info)
        return;

    const pa_card_profile_info2 *voiceCall = nullptr;
    const pa_card_profile_info2 *highest = nullptr;

    for (uint32_t i = 0; i < info->n_profiles; i++)
    {
        const pa_card_profile_info2 *p = info->profiles2[i];
        if (!p || !p->available)
            continue;

        if (!highest || p->priority > highest->priority)
            highest = p;

        /* Dual-SIM devices expose one voicecall profile per modem mode; prefer
         * the explicit mode-1 variant when present, as the Palm-era audiod did,
         * and otherwise take the first plainly-named one. */
        const bool isModeOne = (0 == strcasecmp(p->name, "voicecall-voicemmode1"));
        const bool isPlain = (0 == strcasecmp(p->name, "voicecall") ||
                              0 == strcasecmp(p->name, "voice call") ||
                              0 == strcasecmp(p->name, "Voice Call"));
        if (isModeOne || (!voiceCall && isPlain))
            voiceCall = p;
    }

    if (!voiceCall)
        return;     /* Not the modem card. */

    const char *profileToSet = nullptr;

    if (req->mode == eCallMode_Carrier && info->active_profile2 != voiceCall)
        profileToSet = voiceCall->name;
    else if (req->mode != eCallMode_Carrier && info->active_profile2 == voiceCall && highest)
        profileToSet = highest->name;

    if (profileToSet)
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Setting card '%s' profile to '%s'", info->name, profileToSet);
        pa_operation *op = pa_context_set_card_profile_by_name(c, info->name, profileToSet,
                                                               &PalmLegacyManager::paCardProfileSetCb,
                                                               routingRef(req));
        if (op)
            pa_operation_unref(op);
        else
            routingUnref(req);
    }
}

void PalmLegacyManager::paCardProfileSetCb(pa_context *c, int success, void *userdata)
{
    RoutingRequest *req = static_cast<RoutingRequest*>(userdata);
    if (!req)
        return;

    if (!success)
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to set the card profile; continuing to port selection anyway");

    /* The profile switch replaces the card's sinks and sources, so the port
     * walk has to happen after it completes, not in parallel. */
    pa_operation *op = pa_context_get_sink_info_list(c, &PalmLegacyManager::paSinkInfoCb, req);
    if (op)
        pa_operation_unref(op);
    else
        routingUnref(req);
}

void PalmLegacyManager::paSinkInfoCb(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
{
    RoutingRequest *req = static_cast<RoutingRequest*>(userdata);
    if (!req)
        return;

    if (eol)
    {
        pa_operation *op = pa_context_get_source_info_list(c, &PalmLegacyManager::paSourceInfoCb, req);
        if (op)
            pa_operation_unref(op);
        else
            routingUnref(req);
        return;
    }

    if (!info)
        return;

    pa_sink_port_info *earpiece = nullptr, *speaker = nullptr;
    pa_sink_port_info *wiredHeadset = nullptr, *wiredHeadphone = nullptr;
    pa_sink_port_info *ringHeadset = nullptr, *ringHeadphone = nullptr;
    pa_sink_port_info *scoHeadset = nullptr, *sco = nullptr, *scoCarkit = nullptr;
    pa_sink_port_info *highest = nullptr, *preferred = nullptr;

    for (uint32_t i = 0; i < info->n_ports; i++)
    {
        pa_sink_port_info *p = info->ports[i];
        if (!p)
            continue;

        if (p->available != PA_PORT_AVAILABLE_NO && (!highest || p->priority > highest->priority))
            highest = p;

        if (!strcmp(p->name, "output-earpiece"))
            earpiece = p;
        else if (!strcmp(p->name, "output-speaker"))
            speaker = p;
        else if (!strcmp(p->name, "output-wired_headset") && p->available != PA_PORT_AVAILABLE_NO)
            wiredHeadset = p;
        else if (!strcmp(p->name, "output-wired_headphone") && p->available != PA_PORT_AVAILABLE_NO)
            wiredHeadphone = p;
        /* The droid card's speaker-and-wired ports, for ringing. */
        else if (!strcmp(p->name, "output-speaker+wired_headset") && p->available != PA_PORT_AVAILABLE_NO)
            ringHeadset = p;
        else if (!strcmp(p->name, "output-speaker+wired_headphone") && p->available != PA_PORT_AVAILABLE_NO)
            ringHeadphone = p;
        /* The droid card's Bluetooth SCO devices (AUDIO_DEVICE_OUT_BLUETOOTH_SCO
         * and its headset and car kit kinds). Not by availability: the HAL
         * has no idea whether a headset is connected, BtCall does. */
        else if (!strcmp(p->name, "output-sco_headset"))
            scoHeadset = p;
        else if (!strcmp(p->name, "output-bluetooth_sco"))
            sco = p;
        else if (!strcmp(p->name, "output-sco_carkit"))
            scoCarkit = p;
    }

    /* An earpiece or a loudspeaker is what identifies the device's own sink;
     * anything with neither is some other output (HDMI, USB, a loopback) and
     * is left alone. Tablets have no earpiece, so it cannot be the earpiece
     * alone or their headphone jack is never switched to. */
    if (!earpiece && !speaker)
        return;

    /* The route decides, not port availability by itself: on a device without
     * jack detection every port claims to be available, and an earpiece call
     * must not go out an empty jack. effectiveRoute() only asks for the jack
     * once detection has shown something is in it. */
    switch (req->route)
    {
        case ePhoneRoute_Headset:
        case ePhoneRoute_HeadsetMic:
        {
            /* With detection the two wired ports are exclusive -- the one
             * that is available says whether the plug has a microphone. */
            pa_sink_port_info *wired = (req->route == ePhoneRoute_HeadsetMic)
                                     ? (wiredHeadset ? wiredHeadset : wiredHeadphone)
                                     : (wiredHeadphone ? wiredHeadphone : wiredHeadset);
            /* Ringing with something in the jack: the loudspeaker too, where
             * the card can do both at once. */
            pa_sink_port_info *ring = (wired == wiredHeadset) ? ringHeadset : ringHeadphone;
            if (wired && ring && RingState::speakerToo(req->mode, req->ring))
                wired = ring;
            preferred = wired ? wired : (req->mode != eCallMode_None ? earpiece : nullptr);
            break;
        }
        case ePhoneRoute_Speaker:
            /* Outside a call the loudspeaker is just the highest-priority
             * available port, as it always was. */
            preferred = (req->mode != eCallMode_None) ? speaker : nullptr;
            break;
        case ePhoneRoute_BluetoothSCO:
            /* The HAL carries the call to the headset once its output is a
             * BT SCO device; pulseaudio-modules-droid tells it BT_SCO=on when
             * the port is selected. A HAL without one cannot, so the earpiece
             * rather than the speaker. */
            preferred = scoHeadset ? scoHeadset : (sco ? sco : scoCarkit);
            if (!preferred)
            {
                PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                    "Sink '%s' has no Bluetooth SCO port; call stays on the earpiece", info->name);
                preferred = (req->mode != eCallMode_None) ? earpiece : nullptr;
            }
            break;
        case ePhoneRoute_Earpiece:
        default:
            preferred = (req->mode != eCallMode_None) ? earpiece : nullptr;
            break;
    }

    if (!preferred)
        preferred = highest;

    if (preferred && preferred != info->active_port)
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Setting sink '%s' port to '%s'", info->name, preferred->name);
        pa_operation *op = pa_context_set_sink_port_by_name(c, info->name, preferred->name,
                                                            &PalmLegacyManager::paSinkPortSetCb,
                                                            nullptr);
        if (op)
            pa_operation_unref(op);
    }
}

void PalmLegacyManager::paSinkPortSetCb(pa_context *c, int success, void *userdata)
{
    if (!success)
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "Failed to set the sink port");
}

void PalmLegacyManager::paSourceInfoCb(pa_context *c, const pa_source_info *info, int eol, void *userdata)
{
    RoutingRequest *req = static_cast<RoutingRequest*>(userdata);
    if (!req)
        return;

    if (eol)
    {
        /* End of this chain. */
        routingUnref(req);
        return;
    }

    if (!info)
        return;

    if (info->monitor_of_sink != PA_INVALID_INDEX)
        return;     /* A sink monitor, not a real capture source. */

    pa_source_port_info *builtinMic = nullptr, *headsetMic = nullptr, *btMic = nullptr;
    const pa_source_port_info *preferred = nullptr;

    for (uint32_t i = 0; i < info->n_ports; i++)
    {
        pa_source_port_info *p = info->ports[i];
        if (!p)
            continue;

        if (!strcmp(p->name, "input-builtin_mic"))
            builtinMic = p;
        else if (!strcmp(p->name, "input-wired_headset") && p->available != PA_PORT_AVAILABLE_NO)
            headsetMic = p;
        else if (!strcmp(p->name, "input-bluetooth_sco_headset"))
            btMic = p;
    }

    if (!builtinMic)
        return;     /* Not the handset's own capture source. */

    /* Same as the sink walk: without jack detection every port claims to be
     * available, so the headset mic is only used when the route says one is
     * plugged in. */
    if (req->route == ePhoneRoute_HeadsetMic)
        preferred = headsetMic ? headsetMic : builtinMic;
    else if (req->route == ePhoneRoute_BluetoothSCO)
        preferred = btMic ? btMic : builtinMic;
    else
        preferred = builtinMic;

    if (preferred && preferred != info->active_port)
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Setting source '%s' port to '%s'", info->name, preferred->name);
        pa_operation *op = pa_context_set_source_port_by_name(c, info->name, preferred->name,
                                                              &PalmLegacyManager::paSourcePortSetCb,
                                                              nullptr);
        if (op)
            pa_operation_unref(op);
    }

    /* Keep the capture path muted in step with the call's mute state, so
     * unmuting a call cannot be defeated by a stale port-level mute. */
    if (!!info->mute != !!req->self->mMicMuted)
    {
        pa_operation *op = pa_context_set_source_mute_by_name(c, info->name,
                                                              req->self->mMicMuted ? 1 : 0,
                                                              nullptr, nullptr);
        if (op)
            pa_operation_unref(op);
    }
}

void PalmLegacyManager::paSourcePortSetCb(pa_context *c, int success, void *userdata)
{
    if (!success)
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "Failed to set the source port");
}

/* ------------------------------------------------------------------------- *
 * Headphones and Bluetooth audio
 * ------------------------------------------------------------------------- */

void PalmLegacyManager::paSubscribeCb(pa_context *c, pa_subscription_event_type_t t,
                                      uint32_t idx, void *userdata)
{
    const unsigned int type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK_INPUT)
    {
        PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
        if (!self)
            return;

        if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
        {
            if (self->mRingState.streamGone(idx))
                self->updateCallMode();
            return;
        }

        /* New, or changed (corked, uncorked): what kind of stream it is
         * comes with its info. */
        pa_operation *op = pa_context_get_sink_input_info(c, idx, &PalmLegacyManager::paRingInputCb,
                                                          userdata);
        if (op)
            pa_operation_unref(op);
        return;
    }

    if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) != PA_SUBSCRIPTION_EVENT_SINK)
        return;

    if (type != PA_SUBSCRIPTION_EVENT_CHANGE && type != PA_SUBSCRIPTION_EVENT_NEW)
        return;

    /* A port availability change is a sink change like any other (volume,
     * active port), so look and let paJackSinkCb decide whether it matters. */
    pa_operation *op = pa_context_get_sink_info_by_index(c, idx, &PalmLegacyManager::paJackSinkCb,
                                                         userdata);
    if (op)
        pa_operation_unref(op);
}

void PalmLegacyManager::paJackSinkCb(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self || eol || !info)
        return;

    const pa_sink_port_info *headset = nullptr, *headphone = nullptr;

    for (uint32_t i = 0; i < info->n_ports; i++)
    {
        const pa_sink_port_info *p = info->ports[i];
        if (!p)
            continue;
        if (!strcmp(p->name, "output-wired_headset"))
            headset = p;
        else if (!strcmp(p->name, "output-wired_headphone"))
            headphone = p;
    }

    if (!headset && !headphone)
        return;     /* Not the sink the jack belongs to. */

    EJackState state;

    if (headset && headphone)
    {
        /* droid-card's jack detection, evdev and extcon alike, marks at most
         * one of the two available: the headset port for a plug with a
         * microphone, the headphone port for one without, neither when the
         * jack is empty. A card without detection leaves every port at its
         * default, available -- so both available means "cannot tell". */
        const bool hs = (headset->available == PA_PORT_AVAILABLE_YES);
        const bool hp = (headphone->available == PA_PORT_AVAILABLE_YES);

        if (hs && hp)
            state = eJack_Unknown;
        else if (hs)
            state = eJack_Headset;
        else if (hp)
            state = eJack_Headphones;
        else
            state = eJack_Unplugged;
    }
    else
    {
        /* One wired port only, so the pair cannot vouch for each other: trust
         * "available" once the port has been seen unavailable at least once. */
        const pa_sink_port_info *p = headset ? headset : headphone;

        if (p->available == PA_PORT_AVAILABLE_NO)
        {
            self->mJackSeenUnplugged = true;
            state = eJack_Unplugged;
        }
        else if (p->available == PA_PORT_AVAILABLE_YES && self->mJackSeenUnplugged)
            state = headset ? eJack_Headset : eJack_Headphones;
        else
            state = eJack_Unknown;
    }

    self->setJackState(state);
}

void PalmLegacyManager::paRingInputCb(pa_context *, const pa_sink_input_info *info, int eol,
                                      void *userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self || eol || !info)
        return;

    /* module-palm-policy tags every stream with the virtual sink it was
     * opened on, and keeps the tag when it moves the stream on. */
    const char *mediaType = pa_proplist_gets(info->proplist, "media.type");
    if (self->mRingState.streamSeen(info->index, mediaType, info->corked))
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Ringing %s", self->mRingState.ringing() ? "started" : "stopped");
        self->updateCallMode();
    }
}

void PalmLegacyManager::setJackState(EJackState state)
{
    if (state == mJackState)
        return;

    const bool wasPlugged = jackPlugged();

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Headphone jack state %d -> %d", (int) mJackState, (int) state);
    mJackState = state;

    /* Last connected wins: a plug put in now takes the call from a headset
     * connected before it. Told before the router, whose answer may
     * re-route a call already. */
    if (!wasPlugged && jackPlugged())
        mBtCall.jackPlugged();

    /* audioRouter decides where media goes: of the jack and a Bluetooth
     * headset, the one connected last. The jack is a port of pcm_output, so
     * PulseAudio has no sink of its own to announce for it: tell the router
     * here. It answers at once with the output now in use (handleEvent ->
     * setActiveOutput), so on an unplug mBtAudioSink and mUsbAudioSink
     * below already say whether a headset took the audio over, and a video call
     * re-routed from there (followVideo) sees that headset rather than
     * none and stays off the loudspeaker. Told directly rather
     * than through a module event: the master volume manager would take
     * pcm_headphone for a device with a volume of its own. */
    if (wasPlugged != jackPlugged())
        tellRouterJack();

    /* Pulling the plug is the moment music would otherwise carry on out of
     * the loudspeaker. Only a real unplug counts -- going from unknown to
     * unplugged at start-up is not one. During a call there is nothing
     * playing to pause, but resuming afterwards would now be out loud. With
     * a Bluetooth or USB headset carrying the audio the plug was not playing
     * anything, and the music goes on in that headset. */
    if (wasPlugged && !jackPlugged())
    {
        const CallState::Unplug unplug = CallState::unplugged(mCallMode,
            CallState::privateOutput(mBtAudioSink, false) ||
            CallState::privateOutput(mUsbAudioSink, false));
        if (unplug.pauseMedia)
            pauseMedia(false);
        if (unplug.keepPausedAfterCall)
            mResumeMediaAfterCall = false;
    }

    /* Re-route: effectiveRoute() has changed under the current call mode. */
    updateCallMode();
}

void PalmLegacyManager::tellRouterJack()
{
    AudioRouter *router = AudioRouter::getAudioRouterInstance();
    if (router)
        router->eventDeviceConnectionStatus("pcm_headphone", "pcm_headphone", "",
            jackPlugged() ? utils::eDeviceConnected : utils::eDeviceDisconnected,
            utils::ePulseMixer, true);
}

static bool isBluetoothSink(const std::string &name)
{
    return CallState::isBluetoothOutput(name);
}

/* A USB headset is a sink of its own (usb_speaker0, ...), which audioRouter
 * makes the active output while it is plugged in. Pulling it is an unplug
 * like the jack's when the music would carry on out of the loudspeaker;
 * moving on to Bluetooth or the jack is not. */
void PalmLegacyManager::followUsbOutput(const std::string &sinkName)
{
    if (sinkName == mUsbAudioSink ||
        (!CallState::isUsbOutput(sinkName) && mUsbAudioSink.empty()))
        return;

    const CallState::Unplug unplug = CallState::outputMoved(mCallMode, mUsbAudioSink,
                                                            sinkName, jackPlugged());
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "USB audio %s -> %s", mUsbAudioSink.empty() ? "none" : mUsbAudioSink.c_str(),
        sinkName.c_str());
    mUsbAudioSink = CallState::isUsbOutput(sinkName) ? sinkName : std::string();
    if (unplug.pauseMedia)
        pauseMedia(false);
    if (unplug.keepPausedAfterCall)
        mResumeMediaAfterCall = false;
    notifyStatusSubscribers();
}

/* ------------------------------------------------------------------------- *
 * A USB headset rings on the loudspeaker too -- see usbRing.h
 * ------------------------------------------------------------------------- */

void PalmLegacyManager::syncUsbRing()
{
    /* At most an unload of the sink for a headset gone and a load for the
     * one now: next() says Unload only for a loaded module. */
    for (int step = 0; step < 2; step++)
    {
        const UsbRing::Step next = mUsbRing.next();
        if (next == UsbRing::Step::Unload)
        {
            const uint32_t module = mUsbRing.module();
            mUsbRing.unloading();
            {
                /* The router stops sending to the sink before it goes. */
                AudioRouter *router = AudioRouter::getAudioRouterInstance();
                if (router)
                    router->setClassOverride(ClassRoute::Override{});
            }
            dropUsbRingSink(module);
            continue;
        }
        if (next == UsbRing::Step::Load && mPaReady && mPaContext)
        {
            const std::string args = UsbRing::moduleArguments(mUsbRing.wanted());
            pa_operation *op = pa_context_load_module(mPaContext, "module-combine-sink", args.c_str(),
                                                      &PalmLegacyManager::paUsbRingLoadedCb, this);
            if (op)
            {
                pa_operation_unref(op);
                mUsbRing.loading();
                PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                    "USB ring: loading module-combine-sink %s", args.c_str());
            }
            else
                PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                    "USB ring: could not ask PulseAudio for the combined sink");
        }
        break;
    }

    ClassRoute::Override override;
    if (mUsbRing.ready())
    {
        override.whileOutput = mUsbRing.wanted();
        override.target = UsbRing::SINK;
        override.classes = UsbRing::classes(mCallMode, mRingerOn);
    }
    else
    {
        mBtRing.setOnHeadsetLink(mBtCall.sco() == BtCall::Sco::Up);
        override = mBtRing.classOverride(mCallMode);
    }
    AudioRouter *router = AudioRouter::getAudioRouterInstance();
    if (router)
        router->setClassOverride(override);
}

void PalmLegacyManager::paUsbRingLoadedCb(pa_context *, uint32_t idx, void *userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self)
        return;

    if (idx == PA_INVALID_INDEX)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "USB ring: module-combine-sink did not load; ringtones stay in the USB headset only");
        self->mUsbRing.loadFailed();
    }
    else
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "USB ring: %s is module %u", UsbRing::SINK, idx);
        self->mUsbRing.loaded(idx);
    }
    /* Ready: the router sends the classes to it. Or the headset went while
     * it loaded: it is unloaded again. */
    self->syncUsbRing();
}

void PalmLegacyManager::paUsbRingStaleCb(pa_context *, const pa_sink_info *info, int eol, void *userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self)
        return;

    if (!eol)
    {
        if (info && info->owner_module != PA_INVALID_INDEX)
        {
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "USB ring: %s of an earlier audiod is still there, unloading module %u",
                UsbRing::SINK, info->owner_module);
            self->dropUsbRingSink(info->owner_module);
        }
        return;
    }

    /* Unloads are sent before any load on the same connection. */
    self->mUsbRing.checked();
    self->syncUsbRing();
}

/* What plays on the sink is moved to the output in use first: unloading a
 * sink ends the streams on it, and the router's re-routing goes over another
 * connection, which PulseAudio may well read after this one. */
void PalmLegacyManager::dropUsbRingSink(uint32_t module)
{
    if (!mPaReady || !mPaContext || module == UsbRing::NO_MODULE)
        return;

    mUsbRingDrops.push_back(UsbRingDrop{this, module, PA_INVALID_INDEX,
                                        mActiveSoundOutput.empty() ? std::string(UsbRing::SPEAKER)
                                                                   : mActiveSoundOutput});
    UsbRingDrop *drop = &mUsbRingDrops.back();
    pa_operation *op = pa_context_get_sink_info_by_name(mPaContext, UsbRing::SINK,
                                                        &PalmLegacyManager::paUsbRingDropSinkCb, drop);
    if (op)
        pa_operation_unref(op);
    else
        finishUsbRingDrop(drop);
}

void PalmLegacyManager::paUsbRingDropSinkCb(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
{
    UsbRingDrop *drop = static_cast<UsbRingDrop*>(userdata);
    if (!drop)
        return;

    if (!eol)
    {
        if (info && info->owner_module == drop->module)
            drop->sink = info->index;
        return;
    }

    if (drop->sink == PA_INVALID_INDEX)
    {
        /* No sink by that name (any more) for this module: just unload. */
        drop->self->finishUsbRingDrop(drop);
        return;
    }

    pa_operation *op = pa_context_get_sink_input_info_list(c, &PalmLegacyManager::paUsbRingDropInputCb, drop);
    if (op)
        pa_operation_unref(op);
    else
        drop->self->finishUsbRingDrop(drop);
}

void PalmLegacyManager::paUsbRingDropInputCb(pa_context *c, const pa_sink_input_info *info, int eol,
                                             void *userdata)
{
    UsbRingDrop *drop = static_cast<UsbRingDrop*>(userdata);
    if (!drop)
        return;

    if (!eol)
    {
        if (info && info->sink == drop->sink)
        {
            pa_operation *op = pa_context_move_sink_input_by_name(c, info->index, drop->fallback.c_str(),
                                                                  nullptr, nullptr);
            if (op)
                pa_operation_unref(op);
        }
        return;
    }

    drop->self->finishUsbRingDrop(drop);
}

void PalmLegacyManager::finishUsbRingDrop(UsbRingDrop *drop)
{
    if (mPaReady && mPaContext)
    {
        pa_operation *op = pa_context_unload_module(mPaContext, drop->module, nullptr, nullptr);
        if (op)
            pa_operation_unref(op);
    }
    for (auto it = mUsbRingDrops.begin(); it != mUsbRingDrops.end(); ++it)
    {
        if (&*it == drop)
        {
            mUsbRingDrops.erase(it);
            break;
        }
    }
}

void PalmLegacyManager::setActiveOutput(const std::string &sinkName)
{
    followUsbOutput(sinkName);
    mUsbRing.outputIs(sinkName);
    mBtRing.outputIs(sinkName);
    syncUsbRing();

    if (isBluetoothSink(sinkName))
    {
        if (sinkName == mBtAudioSink)
            return;

        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Bluetooth audio is now carried by %s", sinkName.c_str());
        mBtAudioSink = sinkName;
        mBtAudioName.clear();

        /* The sink description is the name the user gave the device, which
         * is what anything showing "connected to ..." wants. */
        if (mPaReady && mPaContext)
        {
            pa_operation *op = pa_context_get_sink_info_by_name(mPaContext, sinkName.c_str(),
                                                                &PalmLegacyManager::paBtSinkCb,
                                                                this);
            if (op)
                pa_operation_unref(op);
        }

        /* A headset arriving takes a video call off the loudspeaker it
         * went to on its own: see followVideo(). */
        if (mCallMode != eCallMode_None)
            updateCallMode();
        else
            notifyStatusSubscribers();
        return;
    }

    if (mBtAudioSink.empty())
        return;

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Bluetooth audio ended, output is now %s", sinkName.c_str());

    /* Same as pulling the headphone plug: the headset going away (switched
     * off, out of range) would otherwise drop the music onto the speaker.
     * With headphones in, the music moved to them - because they were
     * plugged in after the headset connected, or because the headset went
     * and they are what is left - and goes on there. A USB headset taking
     * over keeps it private as well. The same rule as a USB headset's
     * (followUsbOutput) and the jack's (setJackState). */
    const CallState::Unplug unplug = CallState::outputMoved(mCallMode, mBtAudioSink,
                                                            sinkName, jackPlugged());
    mBtAudioSink.clear();
    mBtAudioName.clear();
    if (unplug.pauseMedia)
        pauseMedia(false);
    if (unplug.keepPausedAfterCall)
        mResumeMediaAfterCall = false;

    /* ... and leaving puts a video call back on the loudspeaker. */
    if (mCallMode != eCallMode_None)
        updateCallMode();
    else
        notifyStatusSubscribers();
}

void PalmLegacyManager::paBtSinkCb(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self || eol || !info || !info->name || !info->description)
        return;

    /* The headset may already be gone again by the time this arrives. */
    if (self->mBtAudioSink != info->name)
        return;

    self->mBtAudioName = info->description;
    self->notifyStatusSubscribers();
}

PalmLegacyManager::EPhoneRoute PalmLegacyManager::effectiveRoute() const
{
    const LegacyCategory *phone = categoryByName("/phone");
    const EPhoneRoute chosen = phone ? phone->route : ePhoneRoute_Earpiece;
    const EPhoneRoute jack = (mJackState == eJack_Headset) ? ePhoneRoute_HeadsetMic
                                                           : ePhoneRoute_Headset;

    if (mCallMode == eCallMode_None)
        return jackPlugged() ? jack : ePhoneRoute_Speaker;

    /* A hands-free headset, once its link is up -- see BtCall for when a
     * call belongs on it. Until then, or when it cannot be had, the call
     * goes where it would without one. */
    if (mBtCall.routed(btSituation()))
        return ePhoneRoute_BluetoothSCO;

    EPhoneRoute fallback = chosen;
    if (fallback == ePhoneRoute_BluetoothSCO)
        fallback = ePhoneRoute_Earpiece;

    /* A plug in the jack takes over from the earpiece, as on any phone; a
     * speakerphone the user picked stays the speakerphone. */
    if (fallback == ePhoneRoute_Earpiece && jackPlugged())
        return jack;

    return fallback;
}

/* ------------------------------------------------------------------------- *
 * Hands-free headsets (HFP) -- see btCall.h and hfpAgent.h
 * ------------------------------------------------------------------------- */

BtCall::Situation PalmLegacyManager::btSituation() const
{
    const LegacyCategory *phone = categoryByName("/phone");
    BtCall::Choice choice = BtCall::Choice::Default;

    switch (phone ? phone->route : ePhoneRoute_Earpiece)
    {
        case ePhoneRoute_Speaker:       choice = BtCall::Choice::Speaker; break;
        case ePhoneRoute_BluetoothSCO:  choice = BtCall::Choice::Bluetooth; break;
        /* A wired route picked by its scenario keeps the call on the wire. */
        case ePhoneRoute_Headset:
        case ePhoneRoute_HeadsetMic:    choice = BtCall::Choice::Handset; break;
        default:                        break;
    }

    return BtCall::Situation{mCallMode, choice, jackPlugged()};
}

void PalmLegacyManager::syncBtCall()
{
    switch (mBtCall.sync(btSituation()))
    {
        case BtCall::Action::Connect:
            if (!mHfpAgent || !mHfpAgent->connectSco(mBtCall.scoCard()))
            {
                PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                    "Could not ask for an SCO link to %s", mBtCall.scoCard().c_str());
                mBtCall.connectFailed();
            }
            break;
        case BtCall::Action::Disconnect:
            if (mHfpAgent)
                mHfpAgent->disconnectSco();
            break;
        case BtCall::Action::None:
        default:
            break;
    }
}

void PalmLegacyManager::dropBluetoothChoice()
{
    LegacyCategory *phone = categoryByName("/phone");
    if (phone && phone->route == ePhoneRoute_BluetoothSCO)
        phone->route = ePhoneRoute_Earpiece;
}

void PalmLegacyManager::hfpCardAdded(const std::string &card, const std::string &remote)
{
    if (!mBtCall.cardAdded(card, remote))
        return;

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Hands-free headset %s connected (%s)", remote.c_str(), card.c_str());
    /* During a call this is where the call moves onto it. */
    updateCallMode();
}

void PalmLegacyManager::hfpCardRemoved(const std::string &card)
{
    if (!mBtCall.cardRemoved(card))
        return;

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Hands-free headset %s disconnected", card.c_str());
    if (!mBtCall.available())
        dropBluetoothChoice();
    updateCallMode();
}

void PalmLegacyManager::hfpScoConnected(const std::string &card, unsigned codec)
{
    switch (mBtCall.scoConnected(card, btSituation()))
    {
        case BtCall::Arrival::Asked:
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "SCO link to %s is up (codec %u), moving the call onto it", card.c_str(), codec);
            break;
        case BtCall::Arrival::ByHeadset:
        {
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "The headset took the call audio (%s)", card.c_str());
            LegacyCategory *phone = categoryByName("/phone");
            if (phone)
                phone->route = ePhoneRoute_BluetoothSCO;
            break;
        }
        case BtCall::Arrival::Unwanted:
        default:
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "SCO link to %s is not wanted, closing it", card.c_str());
            if (mHfpAgent)
                mHfpAgent->disconnectSco();
            break;
    }
    updateCallMode();
}

void PalmLegacyManager::hfpScoConnectFailed(const std::string &card, const std::string &error)
{
    PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "No SCO link to %s (%s); the call stays on the phone", card.c_str(), error.c_str());
    mBtCall.connectFailed();
    updateCallMode();
}

void PalmLegacyManager::hfpScoClosed(const std::string &card)
{
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "The SCO link to %s closed; the call goes back to the phone", card.c_str());
    mBtCall.scoClosed(true);
    dropBluetoothChoice();
    updateCallMode();
}

void PalmLegacyManager::hfpServiceGone()
{
    mBtCall.serviceGone();
    dropBluetoothChoice();
    updateCallMode();
}

/* ------------------------------------------------------------------------- *
 * Pausing media -- through com.webos.service.mediacontroller, the one place
 * every registered media session (web apps through Chromium's media session,
 * native players that register) can be reached from.
 * ------------------------------------------------------------------------- */

namespace {

/* One pause in flight: the session list, then the play status of each
 * session, then -- only if one of them is actually playing -- the key. Asking
 * first is what makes resuming safe: media that was already paused must not
 * start playing when the call ends. */
struct MediaPauseRequest
{
    unsigned int generation;
    bool resumeAfterCall;
    int pending;
    bool playing;
};

}

void PalmLegacyManager::pauseMedia(bool resumeAfterCall)
{
    LSHandle *sh = GetPalmService();
    if (!sh)
        return;

    MediaPauseRequest *req = new(std::nothrow) MediaPauseRequest{mMediaGeneration, resumeAfterCall,
                                                                 0, false};
    if (!req)
        return;

    CLSError lserror;
    if (!LSCallOneReply(sh, MEDIA_SESSIONS_URI, "{}", &PalmLegacyManager::_mediaSessionsCb,
                        req, nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        delete req;
    }
}

bool PalmLegacyManager::_mediaSessionsCb(LSHandle *sh, LSMessage *reply, void *ctx)
{
    MediaPauseRequest *req = static_cast<MediaPauseRequest*>(ctx);
    if (!req)
        return true;

    const char *payload = reply ? LSMessageGetPayload(reply) : nullptr;
    pbnjson::JValue obj = pbnjson::JDomParser::fromString(payload ? payload : "");
    pbnjson::JValue sessions = obj.isObject() ? obj["sessionList"] : pbnjson::JValue();

    /* No session at all is the common case, and mediacontroller answers it
     * with returnValue false -- nothing is playing, nothing to do. */
    if (!sessions.isArray() || sessions.arraySize() == 0)
    {
        delete req;
        return true;
    }

    /* Hold one reference across the loop so a failed call cannot free the
     * request while later ones still need it. */
    req->pending = 1;
    for (ssize_t i = 0; i < sessions.arraySize(); i++)
    {
        std::string mediaId;
        if (sessions[i].asString(mediaId) != CONV_OK || mediaId.empty())
            continue;

        pbnjson::JValue query = pbnjson::Object();
        query.put("mediaId", mediaId);

        CLSError lserror;
        ++req->pending;
        if (!LSCallOneReply(sh, MEDIA_PLAY_STATUS_URI, query.stringify().c_str(),
                            &PalmLegacyManager::_mediaPlayStatusCb, req, nullptr, &lserror))
        {
            lserror.Print(__FUNCTION__, __LINE__);
            --req->pending;
        }
    }

    if (--req->pending == 0)
        delete req;
    return true;
}

bool PalmLegacyManager::_mediaPlayStatusCb(LSHandle *sh, LSMessage *reply, void *ctx)
{
    MediaPauseRequest *req = static_cast<MediaPauseRequest*>(ctx);
    if (!req)
        return true;

    const char *payload = reply ? LSMessageGetPayload(reply) : nullptr;
    pbnjson::JValue obj = pbnjson::JDomParser::fromString(payload ? payload : "");
    std::string playStatus;
    if (obj.isObject() && obj["playStatus"].asString(playStatus) == CONV_OK &&
        playStatus == "PLAYSTATE_PLAYING")
        req->playing = true;

    if (--req->pending > 0)
        return true;

    PalmLegacyManager *self = getPalmLegacyManagerInstance();

    /* A pause for a call that has ended meanwhile must not go out, or it
     * would silence the music the call's end just resumed. */
    const bool stale = req->resumeAfterCall &&
                       (!self || req->generation != self->mMediaGeneration ||
                        self->mCallMode == eCallMode_None);

    if (self && req->playing && !stale)
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Pausing media playback%s", req->resumeAfterCall ? " for a call" : "");
        self->sendMediaKey("pause");
        if (req->resumeAfterCall)
            self->mResumeMediaAfterCall = true;
    }

    delete req;
    return true;
}

void PalmLegacyManager::resumeMedia(bool onHeadsetLink)
{
    /* Whatever pause is still in flight belongs to the call that just ended. */
    ++mMediaGeneration;
    cancelResumeMedia();

    switch (CallState::resumeAfterCall(mResumeMediaAfterCall, onHeadsetLink))
    {
        case CallState::Resume::Never:
            return;
        case CallState::Resume::Later:
            /* mResumeMediaAfterCall stays set until then, so a headset going
             * meanwhile (which clears it) cancels the resume. */
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Resuming the media playback paused for the call in %u ms, "
                "once the headset's call link is down", CallState::resumeDelayMs);
            mResumeMediaTimer = g_timeout_add(CallState::resumeDelayMs,
                                              &PalmLegacyManager::resumeMediaCb, this);
            return;
        case CallState::Resume::Now:
            break;
    }

    mResumeMediaAfterCall = false;
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Resuming the media playback paused for the call");
    sendMediaKey("play");
}

gboolean PalmLegacyManager::resumeMediaCb(gpointer userdata)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(userdata);
    if (!self)
        return G_SOURCE_REMOVE;

    self->mResumeMediaTimer = 0;
    if (!CallState::resumeStillDue(self->mResumeMediaAfterCall, self->mCallMode))
        return G_SOURCE_REMOVE;

    self->mResumeMediaAfterCall = false;
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Resuming the media playback paused for the call");
    self->sendMediaKey("play");
    return G_SOURCE_REMOVE;
}

void PalmLegacyManager::cancelResumeMedia()
{
    if (mResumeMediaTimer)
    {
        g_source_remove(mResumeMediaTimer);
        mResumeMediaTimer = 0;
    }
}

void PalmLegacyManager::sendMediaKey(const char *keyEvent)
{
    LSHandle *sh = GetPalmService();
    if (!sh || !keyEvent)
        return;

    /* displayId is ignored without FEATURE_DUAL_DISPLAY; the key goes to the
     * session that was last active, which is the one that was playing. */
    pbnjson::JValue payload = pbnjson::Object();
    payload.put("displayId", 0);
    payload.put("keyEvent", keyEvent);

    CLSError lserror;
    if (!LSCallOneReply(sh, MEDIA_KEY_URI, payload.stringify().c_str(), nullptr, nullptr,
                        nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not send media key '%s'", keyEvent);
    }
}

void PalmLegacyManager::updateCallMode()
{
    followVideo();

    /* A call takes the ringtones and alerts off the loudspeaker a USB
     * headset rings on too, and the ringtones off the loudspeaker a
     * Bluetooth headset leaves them on; its end puts them back. */
    syncUsbRing();

    /* The SCO link first: effectiveRoute() only puts the call on the
     * headset once the link is up, and a link no longer wanted is closed
     * after the call has been moved off it below. */
    syncBtCall();

    const EPhoneRoute route = effectiveRoute();

    /* The bug this guards against, found by decompiling the webOS 3.0.5 audiod
     * and present in audiod-pro's State::setCallMode() too: the disconnect path
     * reset the software call mode but never pushed it down, so the device's
     * cached "last applied" mode stayed on the just-ended call. The next call
     * then set the same mode, this early-return matched, and the whole route
     * switch was skipped -- audio silently failed from the second call onwards.
     *
     * The route is part of the comparison as well, or a speakerphone toggle
     * during a call (which changes the route but not the mode) would be
     * swallowed here and never reach the hardware.
     *
     * Subscribers are told either way: the routing may be unchanged while
     * what they see (the jack, a call starting with the speaker already on)
     * is not. */
    if (mCallMode == mAppliedCallMode && route == mAppliedRoute &&
        mRingState.ringing() == mAppliedRing)
    {
        PM_LOG_DEBUG("updateCallMode: mode %d / route %d unchanged, nothing to apply",
            (int) mCallMode, (int) route);
        notifyStatusSubscribers();
        return;
    }

    if (applyCallRouting(mCallMode, route))
    {
        mAppliedCallMode = mCallMode;
        mAppliedRoute = route;
        mAppliedRing = mRingState.ringing();
    }
    else
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to apply call routing for mode %d; leaving applied mode at %d "
            "so the next transition retries", (int) mCallMode, (int) mAppliedCallMode);
    }

    notifyStatusSubscribers();
}

void PalmLegacyManager::followVideo()
{
    LegacyCategory *phone = categoryByName("/phone");
    if (!phone)
        return;

    /* Only a VoIP call has video of ours; a carrier call that takes over
     * from a video call goes back to the ear. */
    VideoSpeaker::Situation now{};
    now.videoCall = (mCallMode == eCallMode_Voip) && mCallState.video();
    now.onEarpiece = (phone->route == ePhoneRoute_Earpiece);
    now.onSpeaker = (phone->route == ePhoneRoute_Speaker);
    now.headset = jackPlugged() || !mBtAudioSink.empty() || mBtCall.available();
    now.noSpeaker = mDisabledScenarios.count(scenarioName(phone, ePhoneRoute_Speaker)) > 0;

    const VideoSpeaker::Move move = mVideoSpeaker.update(now);
    if (move == VideoSpeaker::eMove_None)
        return;

    phone->route = (move == VideoSpeaker::eMove_Speaker) ? ePhoneRoute_Speaker
                                                         : ePhoneRoute_Earpiece;
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "Video call: %s", move == VideoSpeaker::eMove_Speaker
            ? "on the loudspeaker" : "back to the earpiece");
    notifyCategory(phone, "changed");
}

void PalmLegacyManager::applyCallStatus()
{
    const ECallMode previous = mCallMode;
    mCallMode = mCallState.mode();
    const CallState::Transition change = CallState::transition(previous, mCallMode);

    /* Speakerphone belongs to the call it was picked for: the next call
     * starts on the earpiece (or the headphones) again, as every phone does,
     * whether or not the caller remembers to say so. */
    if (change.resetSpeaker)
    {
        LegacyCategory *phone = categoryByName("/phone");
        if (phone && (phone->route == ePhoneRoute_Speaker ||
                      phone->route == ePhoneRoute_BluetoothSCO))
            phone->route = ePhoneRoute_Earpiece;
        mVideoSpeaker.callEnded();
        /* So does a headset chosen, given back or failed for it. */
        mBtCall.callEnded();
    }

    /* Mute belongs to the call too. A call hung up while muted left the
     * handset mic muted, so every recording and VoIP call after it was
     * silent. Cleared before the routing below, whose source walk then
     * unmutes the capture path. */
    if (change.resetMute)
    {
        mMicMuted = false;
        mPhoneMuted = false;
    }

    /* Whether the call was on the headset's link, which the routing below
     * closes when the last call ends. */
    const bool onHeadsetLink = (mBtCall.sco() != BtCall::Sco::Down);

    /* Deliberately called on the disconnect path too -- see updateCallMode(). */
    updateCallMode();

    /* Media gives way to the call and comes back after it, the way it does
     * on any phone. After the routing, so the music resumes out of whatever
     * the call ended on rather than briefly out of the earpiece. */
    if (change.pauseMedia)
        pauseMedia(true);
    else if (change.resumeMedia)
        resumeMedia(onHeadsetLink);
}

/* ------------------------------------------------------------------------- *
 * Status payloads and subscriptions
 * ------------------------------------------------------------------------- */

void PalmLegacyManager::postCategoryStatus(const std::string &key, pbnjson::JValue reply)
{
    CLSError lserror;
    std::string payload = reply.stringify();

    if (mPalmHandle && !LSSubscriptionReply(mPalmHandle, key.c_str(), payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    if (mPortsHandle && !LSSubscriptionReply(mPortsHandle, key.c_str(), payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
}

pbnjson::JValue PalmLegacyManager::rootStatus() const
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", mMasterVolume);
    reply.put("mute", mMasterMuted);
    reply.put("muted", mMasterMuted);
    reply.put("inCall", mCallMode != eCallMode_None);
    reply.put("speakerMode", categoryByName("/phone") &&
                             categoryByName("/phone")->route == ePhoneRoute_Speaker);
    reply.put("micMute", mMicMuted);
    reply.put("videoCall", mCallMode == eCallMode_Voip && mCallState.video());

    /* Where audio goes: the shell's headphone indicator and the phone app's
     * route button read these. headsetState is "unknown" both before the
     * first look and on a device without jack detection. */
    static const char *const jackNames[] = { "unknown", "unplugged", "headphones", "headset" };
    reply.put("headset", jackPlugged());
    reply.put("headsetMic", mJackState == eJack_Headset);
    reply.put("headsetState", jackNames[mJackState]);
    reply.put("bluetoothAudio", !mBtAudioSink.empty());
    reply.put("bluetoothAudioName", mBtAudioName);
    reply.put("usbAudio", !mUsbAudioSink.empty());

    /* Hands-free: a headset the call can go to (the phone app's route
     * button offers it), and whether the call is on it now. */
    reply.put("bluetoothCall", mBtCall.available());
    reply.put("bluetoothCallAddress", mBtCall.address());
    reply.put("bluetoothCallActive", mCallMode != eCallMode_None &&
                                     mAppliedRoute == ePhoneRoute_BluetoothSCO);
    return reply;
}

/* The shape com.palm.app.phone's audioInterface.js actually reads: it gates on
 * payload.action and payload.active, and without them its route picker and the
 * in-call proximity handling never update. */
pbnjson::JValue PalmLegacyManager::categoryStatus(const LegacyCategory *cat,
                                                  const char *action) const
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    if (!cat)
        return reply;

    const bool routable = (cat->scenarioPrefix != nullptr);
    const bool isPhone = (0 == strcmp(cat->category, "/phone"));

    reply.put("action", action ? action : "changed");
    reply.put("volume", categoryVolume(cat));
    reply.put("muted", isPhone ? mPhoneMuted : categoryMuted(cat));

    if (routable)
    {
        reply.put("scenario", scenarioName(cat, cat->route));
        /* "active" means this scenario is the one currently carrying audio. */
        reply.put("active", isPhone ? (mCallMode != eCallMode_None) : true);
    }

    if (isPhone)
    {
        reply.put("hac", mHac);
        reply.put("inCall", mCallMode != eCallMode_None);
    }

    /* com.palm.app.clock reads the ringer switch out of /system/status. */
    if (0 == strcmp(cat->category, "/system"))
        reply.put("ringer switch", mRingerOn);

    return reply;
}

void PalmLegacyManager::notifyCategory(const LegacyCategory *cat, const char *action)
{
    if (!cat)
        return;
    postCategoryStatus(std::string(cat->category) + "/status", categoryStatus(cat, action));
}

void PalmLegacyManager::notifyStatusSubscribers()
{
    postCategoryStatus(KEY_ROOT_STATUS, rootStatus());
    postCategoryStatus("/status", rootStatus());

    /* Every category, not just /phone: subscribers on /ringtone/status,
     * /media/status, /system/status and /vvm/status used to get one reply and
     * then silence forever. */
    for (LegacyCategory *c = sCategories; c->category; ++c)
        notifyCategory(c, "changed");
}

/* ------------------------------------------------------------------------- *
 * Root category
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_getStatus(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = self ? self->rootStatus() : pbnjson::Object();
    if (!self)
        reply.put("returnValue", true);
    if (subscribed)
        reply.put("subscribed", true);

    replyJson(sh, message, reply);
    return true;
}

/* ------------------------------------------------------------------------- *
 * Settings: getSettings / setSettings, kept in settingsservice
 * ------------------------------------------------------------------------- */

pbnjson::JValue PalmLegacyManager::settingsReply() const
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put(SETTING_HEADSET_TAKES_CALL, mHeadsetTakesCall.value());
    reply.put(SETTING_ALERTS_ON_SPEAKER, mAlertsOnSpeaker.value());
    return reply;
}

void PalmLegacyManager::applySettings()
{
    mBtCall.setTakesCallOnConnect(mHeadsetTakesCall.value());
    if (mBtRing.alertsOnSpeaker() != mAlertsOnSpeaker.value())
    {
        mBtRing.setAlertsOnSpeaker(mAlertsOnSpeaker.value());
        syncUsbRing();
    }
    postCategoryStatus(KEY_ROOT_SETTINGS, settingsReply());
}

void PalmLegacyManager::readStoredSettings()
{
    CLSError lserror;
    LSHandle *sh = GetPalmService();
    pbnjson::JValue keys = pbnjson::Array();
    keys.append(SETTING_HEADSET_TAKES_CALL);
    keys.append(SETTING_ALERTS_ON_SPEAKER);
    keys.append(SETTING_RINGTONE_VOLUME);
    keys.append(SETTING_RINGTONE_MUTED);
    pbnjson::JValue payload = pbnjson::Object();
    payload.put("category", SETTINGS_CATEGORY);
    payload.put("keys", keys);
    if (!sh || !LSCallOneReply(sh, SETTINGS_GET, payload.stringify().c_str(),
                               &PalmLegacyManager::_storedSettingsCb, this, nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not read the stored settings; using the defaults");
    }
}

bool PalmLegacyManager::_storedSettingsCb(LSHandle *, LSMessage *reply, void *ctx)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(ctx);
    const char *payload = reply ? LSMessageGetPayload(reply) : nullptr;
    if (!self || !payload)
        return true;

    /* A key never stored comes back missing (or the call fails): the
     * default stays. */
    pbnjson::JValue obj = pbnjson::JDomParser::fromString(payload);
    if (!obj.isObject() || !obj["settings"].isObject())
        return true;
    const pbnjson::JValue settings = obj["settings"];
    bool changed = false;
    for (auto [key, setting] : {std::pair{SETTING_HEADSET_TAKES_CALL, &self->mHeadsetTakesCall},
                                std::pair{SETTING_ALERTS_ON_SPEAKER, &self->mAlertsOnSpeaker}})
    {
        bool value = false;
        if (settings[key].isBoolean() && settings[key].asBool(value) == CONV_OK)
        {
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Stored setting %s: %d", key, (int) value);
            if (setting->stored(value))
                changed = true;
        }
    }
    if (changed)
        self->applySettings();

    bool ringtoneChanged = false;
    if (const std::optional<int> volume = SettingsRequest::storedVolume(settings[SETTING_RINGTONE_VOLUME]))
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Stored setting %s: %d", SETTING_RINGTONE_VOLUME, *volume);
        ringtoneChanged = self->mRingtoneVolume.stored(*volume) || ringtoneChanged;
    }
    if (const std::optional<bool> muted = SettingsRequest::storedFlag(settings[SETTING_RINGTONE_MUTED]))
    {
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Stored setting %s: %d", SETTING_RINGTONE_MUTED, (int) *muted);
        ringtoneChanged = self->mRingtoneMuted.stored(*muted) || ringtoneChanged;
    }
    if (ringtoneChanged)
        self->applyRingtoneVolume();
    return true;
}

/* The stored ringtone volume and mute onto the pringtones stream, a ring
 * already playing included (setStreamVolume reprograms its sink inputs). */
void PalmLegacyManager::applyRingtoneVolume()
{
    LegacyCategory *ringtone = categoryByName("/ringtone");
    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    if (!ringtone || !ringtone->streamType || !policy)
        return;
    policy->setStreamVolume(ringtone->streamType, mRingtoneVolume.value());
    policy->setStreamMute(ringtone->streamType, mRingtoneMuted.value());
    notifyCategory(ringtone, "changed");
}

static constexpr guint RINGTONE_STORE_DELAY_MS = 1000;

void PalmLegacyManager::storeRingtoneVolume(bool volume, bool muted)
{
    mStoreRingtoneVolume = mStoreRingtoneVolume || volume;
    mStoreRingtoneMuted = mStoreRingtoneMuted || muted;
    if (mStoreRingtoneTimer)
        g_source_remove(mStoreRingtoneTimer);
    mStoreRingtoneTimer = g_timeout_add(RINGTONE_STORE_DELAY_MS,
                                        &PalmLegacyManager::storeRingtoneVolumeCb, this);
}

gboolean PalmLegacyManager::storeRingtoneVolumeCb(gpointer data)
{
    PalmLegacyManager *self = static_cast<PalmLegacyManager*>(data);
    if (self)
    {
        self->mStoreRingtoneTimer = 0;
        self->writeRingtoneVolume();
    }
    return G_SOURCE_REMOVE;
}

void PalmLegacyManager::writeRingtoneVolume()
{
    if (!mStoreRingtoneVolume && !mStoreRingtoneMuted)
        return;
    CLSError lserror;
    LSHandle *sh = GetPalmService();
    /* Only what changed: the other may not have been read back yet. */
    pbnjson::JValue settings = pbnjson::Object();
    if (mStoreRingtoneVolume)
        settings.put(SETTING_RINGTONE_VOLUME, mRingtoneVolume.value());
    if (mStoreRingtoneMuted)
        settings.put(SETTING_RINGTONE_MUTED, mRingtoneMuted.value());
    mStoreRingtoneVolume = false;
    mStoreRingtoneMuted = false;
    pbnjson::JValue payload = pbnjson::Object();
    payload.put("category", SETTINGS_CATEGORY);
    payload.put("settings", settings);
    if (!sh || !LSCall(sh, SETTINGS_SET, payload.stringify().c_str(), nullptr, nullptr, nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not store the ringtone volume; it lasts until audiod restarts");
    }
}

void PalmLegacyManager::storeSettings(const SettingsRequest::Change &change)
{
    CLSError lserror;
    LSHandle *sh = GetPalmService();
    /* Only what the request set: the other may not have been read back yet,
     * and its default would overwrite what is stored. */
    pbnjson::JValue settings = pbnjson::Object();
    if (change.headsetTakesCall)
        settings.put(SETTING_HEADSET_TAKES_CALL, mHeadsetTakesCall.value());
    if (change.alertsOnSpeaker)
        settings.put(SETTING_ALERTS_ON_SPEAKER, mAlertsOnSpeaker.value());
    pbnjson::JValue payload = pbnjson::Object();
    payload.put("category", SETTINGS_CATEGORY);
    payload.put("settings", settings);
    if (!sh || !LSCall(sh, SETTINGS_SET, payload.stringify().c_str(), nullptr, nullptr, nullptr, &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not store the settings; they last until audiod restarts");
    }
}

bool PalmLegacyManager::_getSettings(LSHandle *sh, LSMessage *message, void * /*ctx*/)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = self ? self->settingsReply() : pbnjson::Object();
    if (!self)
        reply.put("returnValue", false);
    if (subscribed)
        reply.put("subscribed", true);

    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_setSettings(LSHandle *sh, LSMessage *message, void * /*ctx*/)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SettingsRequest::SET_SCHEMA);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Refused, not a default applied: '{}' turned the setting off. */
    const std::optional<SettingsRequest::Change> change = SettingsRequest::change(msg.get());
    if (!change)
    {
        CLSError lserror;
        if (!LSMessageReply(sh, message, STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                "bluetoothHeadsetTakesCall or bluetoothAlertsOnSpeaker must be true or false"), &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    if (self)
    {
        bool changed = false;
        if (change->headsetTakesCall)
        {
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Setting %s: %d", SETTING_HEADSET_TAKES_CALL, (int) *change->headsetTakesCall);
            if (self->mHeadsetTakesCall.set(*change->headsetTakesCall))
                changed = true;
        }
        if (change->alertsOnSpeaker)
        {
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Setting %s: %d", SETTING_ALERTS_ON_SPEAKER, (int) *change->alertsOnSpeaker);
            if (self->mAlertsOnSpeaker.set(*change->alertsOnSpeaker))
                changed = true;
        }
        /* Stored also when unchanged: the stored value may still be the
         * default's absence, or older than what the user sees. */
        self->storeSettings(*change);
        if (changed)
            self->applySettings();
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_rootSetVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, LegacyRequest::SET_VOLUME);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Not 0 for a volume that is not there: '{}' silenced the output. */
    const std::optional<int32_t> requested = LegacyRequest::integer(msg.get(), "volume");
    if (!requested)
    {
        replyInvalid(sh, message, "volume must be an integer");
        return true;
    }
    const int volume = *requested;

    if (volume < 0 || volume > 100)
    {
        CLSError lserror;
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                "Volume out of range. Must be in [0;100]");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    if (self)
    {
        pbnjson::JValue payload = pbnjson::Object();
        payload.put("volume", volume);
        self->masterVolumeCall("setVolume", payload);
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_rootGetVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", self ? self->mMasterVolume : 0);
    reply.put("muted", self ? self->mMasterMuted : false);
    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_setMute(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(mute, boolean),
                                              OPTIONAL(muted, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool mute = false;
    if (!msg.get("mute", mute))
        msg.get("muted", mute);

    if (self)
    {
        pbnjson::JValue payload = pbnjson::Object();
        payload.put("mute", mute);
        self->masterVolumeCall("muteVolume", payload);
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_setMicMute(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(OPTIONAL(micMute, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool micMute = false;
    msg.get("micMute", micMute);

    if (self)
    {
        self->mMicMuted = micMute;
        /* Push it at the capture path rather than only recording it: the
         * source walk applies mMicMuted to the handset's own source. */
        self->applyCallRouting(self->mCallMode, self->effectiveRoute());
        self->notifyStatusSubscribers();
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_setCallMode(LSHandle *sh, LSMessage *message, void *ctx)
{
    /* The org.webosports.service.audio spelling used by the QML phone app and
     * kept for anything still calling it. phone/CallStatusUpdate is the richer
     * path and the one com.palm.app.phone uses. inCall is the phone app's
     * cellular call only: VoIP calls are reported by their connectors. */
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_3(OPTIONAL(inCall, boolean),
                                              OPTIONAL(speakerMode, boolean),
                                              OPTIONAL(route, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* route, where the call's audio goes: "speaker", "handset" (the
     * earpiece, also with a headset connected), "bluetooth" (a hands-free
     * headset), or "default" (the earpiece, the jack or a headset, whichever
     * was connected last). speakerMode true is "speaker", false "default". */
    const pbnjson::JValue routeArg = msg.get()["route"];
    BtCall::Choice choice = BtCall::Choice::Default;
    if (routeArg.isString())
    {
        const std::string r = routeArg.asString();
        if (r == "speaker")
            choice = BtCall::Choice::Speaker;
        else if (r == "handset")
            choice = BtCall::Choice::Handset;
        else if (r == "bluetooth")
            choice = BtCall::Choice::Bluetooth;
        else if (r != "default")
        {
            CLSError lserror;
            const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                "route must be speaker, handset, bluetooth or default");
            if (!LSMessageReply(sh, message, reply, &lserror))
                lserror.Print(__FUNCTION__, __LINE__);
            return true;
        }
    }

    if (self)
    {
        LegacyCategory *phone = categoryByName("/phone");

        /* Only what the caller actually sent: {"speakerMode":true} on its own
         * is the speaker button mid-call and must leave the call up. Not
         * msg.get() unguarded - pbnjson's asBool() writes false into the
         * target for a missing key, so the call ended on every speaker
         * toggle. */
        pbnjson::JValue payload = msg.get();
        if (payload["inCall"].isBoolean())
            self->mCallState.setCarrierCall(payload["inCall"].asBool());
        if (payload["speakerMode"].isBoolean() && !routeArg.isString())
            choice = payload["speakerMode"].asBool() ? BtCall::Choice::Speaker
                                                     : BtCall::Choice::Default;
        /* With inCall it is the phone app starting a cellular call on the
         * earpiece, not a choice the video call has to respect. */
        if ((routeArg.isString() || payload["speakerMode"].isBoolean()) &&
            !payload["inCall"].isBoolean())
            self->mVideoSpeaker.userPicked();

        /* Neither: only the call state changed, and the route stays as
         * it was. */
        if (phone && (routeArg.isString() || payload["speakerMode"].isBoolean()))
        {
            switch (choice)
            {
                case BtCall::Choice::Speaker:   phone->route = ePhoneRoute_Speaker; break;
                case BtCall::Choice::Bluetooth: phone->route = ePhoneRoute_BluetoothSCO; break;
                default:                        phone->route = ePhoneRoute_Earpiece; break;
            }
            self->mBtCall.chose(choice);
        }
        else if (phone && phone->route != ePhoneRoute_Speaker &&
                 phone->route != ePhoneRoute_BluetoothSCO)
        {
            /* What it always did with only inCall: anything but the
             * speaker (or now the headset) back to the earpiece. */
            phone->route = ePhoneRoute_Earpiece;
        }

        self->applyCallStatus();
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_volumeUp(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    if (self)
        self->masterVolumeCall("volumeUp", pbnjson::Object());
    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_volumeDown(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    if (self)
        self->masterVolumeCall("volumeDown", pbnjson::Object());
    replySuccess(sh, message);
    return true;
}

/* ------------------------------------------------------------------------- *
 * Category-addressed methods
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_status(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = self ? self->categoryStatus(cat, "changed") : pbnjson::Object();
    if (!self)
        reply.put("returnValue", true);
    if (subscribed)
        reply.put("subscribed", true);

    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_setVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);

    /* scenario is optional in the Palm API and callers do send it; rejecting it
     * as an unknown property (SCHEMA_n sets additionalProperties:false) is what
     * made per-scenario volume sets fail outright. */
    LSMessageJsonParser msg(message, LegacyRequest::SET_VOLUME);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Not 0 for a volume that is not there: '{}' silenced the output. */
    const std::optional<int32_t> requested = LegacyRequest::integer(msg.get(), "volume");
    if (!requested)
    {
        replyInvalid(sh, message, "volume must be an integer");
        return true;
    }
    const int volume = *requested;

    CLSError lserror;
    if (volume < 0 || volume > 100)
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                "Volume out of range. Must be in [0;100]");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    if (!self || !cat || !cat->streamType || !policy ||
        !policy->setStreamVolume(cat->streamType, volume))
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INTERNAL_ERROR,
                                                "Could not set the volume for this category");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    if (cat == categoryByName("/ringtone"))
    {
        self->mRingtoneVolume.set(volume);
        self->storeRingtoneVolume(true, false);
    }
    self->notifyCategory(cat, "changed");
    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_getVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", self ? self->categoryVolume(cat) : 0);
    reply.put("muted", self ? self->categoryMuted(cat) : false);
    if (cat && cat->scenarioPrefix)
        reply.put("scenario", scenarioName(cat, cat->route));

    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_offsetVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, LegacyRequest::OFFSET_VOLUME);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    const std::optional<int32_t> requested = LegacyRequest::integer(msg.get(), "offset");
    if (!requested)
    {
        replyInvalid(sh, message, "offset must be an integer");
        return true;
    }
    const int offset = *requested;

    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    CLSError lserror;
    if (!self || !cat || !cat->streamType || !policy)
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INTERNAL_ERROR,
                                                "Could not offset the volume for this category");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    int volume = self->categoryVolume(cat) + offset;
    if (volume < 0)
        volume = 0;
    else if (volume > 100)
        volume = 100;

    policy->setStreamVolume(cat->streamType, volume);
    if (cat == categoryByName("/ringtone"))
    {
        self->mRingtoneVolume.set(volume);
        self->storeRingtoneVolume(true, false);
    }
    self->notifyCategory(cat, "changed");

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", volume);
    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_setMuted(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, LegacyRequest::SET_MUTED);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Not false for a muted that is not there: '{}' unmuted the category. */
    const std::optional<bool> requested = LegacyRequest::boolean(msg.get(), "muted");
    if (!requested)
    {
        replyInvalid(sh, message, "muted must be true or false");
        return true;
    }
    const bool muted = *requested;

    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    if (self && cat && cat->streamType && policy)
    {
        policy->setStreamMute(cat->streamType, muted);
        if (cat == categoryByName("/ringtone"))
        {
            self->mRingtoneMuted.set(muted);
            self->storeRingtoneVolume(false, true);
        }
        self->notifyCategory(cat, "changed");
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_listScenarios(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(enabled, boolean),
                                              OPTIONAL(disabled, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    pbnjson::JValue scenarios = pbnjson::Array();
    if (self)
    {
        for (const auto &name : self->scenariosFor(cat))
            scenarios.append(name);
    }

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("scenarios", scenarios);
    if (cat && cat->scenarioPrefix)
        reply.put("scenario", scenarioName(cat, cat->route));

    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_setCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, LegacyRequest::SCENARIO);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    const std::string scenario = LegacyRequest::string(msg.get(), "scenario").value_or(std::string());

    EPhoneRoute route = ePhoneRoute_Earpiece;
    CLSError lserror;
    if (!self || !cat || !routeFromScenario(cat, scenario, &route))
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                "Unknown scenario for this category");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    if (self->mDisabledScenarios.find(scenario) != self->mDisabledScenarios.end())
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                "That scenario is disabled");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    cat->route = route;

    if (0 == strcmp(cat->category, "/phone"))
    {
        self->mVideoSpeaker.userPicked();
        /* The Palm phone app's route picker: the front speaker picked
         * there is the handset, also with a headset connected. */
        self->mBtCall.chose(route == ePhoneRoute_BluetoothSCO ? BtCall::Choice::Bluetooth
                          : route == ePhoneRoute_Speaker      ? BtCall::Choice::Speaker
                          : route == ePhoneRoute_Earpiece     ? BtCall::Choice::Handset
                                                              : BtCall::Choice::Default);
        /* The decompiled 3.0.5 audiod dropped routing changes outright unless
         * the call was already Active, which silently lost every scenario
         * switch made while a call was still ringing or dialing. Push it
         * through updateCallMode() instead, which now compares the route as
         * well as the mode, so a mid-call speakerphone toggle is applied and a
         * change made while ringing is picked up on the Active transition. */
        self->updateCallMode();
    }
    else
    {
        /* Output-device selection for non-call audio belongs to audioRouter in
         * this generation, and its device names are per-machine. The scenario
         * is tracked and reported -- which is what com.palm.app.phone's route
         * picker reads -- but deliberately not turned into a card/port change
         * here, where it would be a guess. */
        PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "setCurrentScenario(%s): recorded for %s; output device selection is "
            "audioRouter's to make", scenario.c_str(), cat->category);
    }

    self->notifyCategory(cat, "changed");
    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_getCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx)
{
    LegacyCategory *cat = categoryFor(message);

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("scenario", cat ? scenarioName(cat, cat->route) : std::string());
    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_lockVolumeKeys(LSHandle *sh, LSMessage *message, void *ctx)
{
    /* The music player subscribes to this to claim the hardware volume keys for
     * as long as it is foregrounded. Tracked here; key routing itself lives in
     * com.palm.keymanager. */
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    if (self)
        self->mVolumeLocked = subscribed;

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    if (subscribed)
        reply.put("subscribed", true);

    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_vvmControl(LSHandle *sh, LSMessage *message, void *ctx)
{
    /* The voicemail drawer's speakerphone button: {"active":true} asks for the
     * back speaker, false for the earpiece. */
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(active, boolean),
                                              OPTIONAL(scenario, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool active = false;
    std::string scenario;

    if (self && cat)
    {
        if (msg.get("scenario", scenario))
        {
            EPhoneRoute route = ePhoneRoute_Earpiece;
            if (routeFromScenario(cat, scenario, &route))
                cat->route = route;
        }
        else if (msg.get("active", active))
        {
            cat->route = active ? ePhoneRoute_Speaker : ePhoneRoute_Earpiece;
        }
        self->notifyCategory(cat, "changed");
    }

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    if (cat)
        reply.put("scenario", scenarioName(cat, cat->route));
    replyJson(sh, message, reply);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /phone
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_phoneSetMuted(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, LegacyRequest::SET_MUTED);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Not false for a muted that is not there: '{}' opened the microphone
     * of a muted call. */
    const std::optional<bool> requested = LegacyRequest::boolean(msg.get(), "muted");
    if (!requested)
    {
        replyInvalid(sh, message, "muted must be true or false");
        return true;
    }
    const bool muted = *requested;

    if (self)
    {
        self->mPhoneMuted = muted;
        /* On Palm hardware muting the call muted the capture path, not the
         * playback one -- the far end stops hearing you, you keep hearing them. */
        self->mMicMuted = muted;
        self->applyCallRouting(self->mCallMode, self->effectiveRoute());
        self->notifyStatusSubscribers();
    }

    replySuccess(sh, message);
    return true;
}

/*
 * phone/CallStatusUpdate
 *
 * com.palm.app.phone sends {"lines":[...]}, one entry per line, each carrying a
 * "state" and a "calls" array whose first element names the transport. This is
 * the contract audiod-pro's own _callStatusUpdate implements and the one
 * CallSynergizer.js actually speaks; the {transport,status} shape this module
 * used to declare matched nothing, and because SCHEMA_n sets
 * additionalProperties:false every real update was rejected before it arrived.
 *
 * The messaging connectors in imlibpurpleservice (WhatsApp, Telegram, Signal,
 * Teams) send the same shape for their VoIP calls, with their own transport.
 */
bool PalmLegacyManager::_callStatusUpdate(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, LegacyRequest::CALL_STATUS);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    if (!self)
    {
        replySuccess(sh, message);
        return true;
    }

    pbnjson::JValue lines = msg.get()["lines"];
    if (!lines.isArray())
    {
        replyInvalid(sh, message, "lines must be an array");
        return true;
    }

    /* Each sender's update replaces only what that sender reported before:
     * see CallState. A sender without a bus name (luna-send) is told apart by
     * its unique name. */
    const char *reporter = LSMessageGetSenderServiceName(message);
    if (!reporter)
        reporter = LSMessageGetSender(message);

    std::vector<CallState::Line> reported;
    for (ssize_t i = 0; i < lines.arraySize(); i++)
    {
        std::string state;
        CallState::Line line{eCallStatus_NoCall, std::string(), false};

        if (lines[i]["state"].asString(state) != CONV_OK)
        {
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "CallStatusUpdate: line %zd has no state", (ssize_t) i);
            continue;
        }
        line.status = CallState::statusFromString(state);

        pbnjson::JValue calls = lines[i]["calls"];
        if (calls.isArray() && calls.arraySize() > 0)
        {
            calls[0]["transport"].asString(line.transport);
            calls[0]["video"].asBool(line.video);
        }

        bool outgoingVideo = false, incomingVideo = false;
        lines[i]["outgoingVideo"].asBool(outgoingVideo);
        lines[i]["incomingVideo"].asBool(incomingVideo);
        line.video = line.video || outgoingVideo || incomingVideo;

        reported.push_back(line);
    }

    /* An empty "lines" is "no calls", from the phone app's TouchPad days on;
     * CallState takes it per sender like any other update. */
    self->mCallState.report(reporter ? reporter : "", reported);
    self->applyCallStatus();

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_hacSet(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    /* The phone app's accessibility panel sends {"enable":bool}; "hac" was
     * never the parameter name. */
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(enable, boolean),
                                              OPTIONAL(hac, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool hac = false;
    if (!msg.get("enable", hac))
        msg.get("hac", hac);

    if (self && self->mHac != hac)
    {
        self->mHac = hac;
        /* Not just a flag any more: this takes the speakerphone out of service
         * and puts the call on the earpiece. */
        self->applyHac();
        self->notifyStatusSubscribers();
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_hacGet(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    /* webOS 3.0.5's _hacGet is subscribable and answers with "enabled", not
     * "hac" -- "hac" is the spelling the /phone/status payload uses. */
    if (LSMessageIsSubscription(message) &&
        !LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("enabled", self ? self->mHac : false);
    reply.put("subscribed", subscribed);
    replyJson(sh, message, reply);
    return true;
}

bool PalmLegacyManager::_enableScenario(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, LegacyRequest::SCENARIO);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    const std::string scenario = LegacyRequest::string(msg.get(), "scenario").value_or(std::string());

    EPhoneRoute route = ePhoneRoute_Earpiece;
    CLSError lserror;
    if (!self || !cat || !routeFromScenario(cat, scenario, &route))
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                "Unknown scenario for this category");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    self->mDisabledScenarios.erase(scenario);
    self->notifyCategory(cat, "enabled");
    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_disableScenario(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, LegacyRequest::SCENARIO);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    const std::string scenario = LegacyRequest::string(msg.get(), "scenario").value_or(std::string());

    EPhoneRoute route = ePhoneRoute_Earpiece;
    CLSError lserror;
    if (!self || !cat || !routeFromScenario(cat, scenario, &route))
    {
        const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                "Unknown scenario for this category");
        if (!LSMessageReply(sh, message, reply, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        return true;
    }

    self->mDisabledScenarios.insert(scenario);

    /* Disabling the scenario currently in use has to move the audio somewhere,
     * or the category is left pointing at a route it may no longer have. */
    if (cat->route == route)
    {
        cat->route = ePhoneRoute_Earpiece;
        if (0 == strcmp(cat->category, "/phone"))
            self->updateCallMode();
    }

    self->notifyCategory(cat, "disabled");
    replySuccess(sh, message);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /telephony
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_telephonyAnswered(LSHandle *sh, LSMessage *message, void *ctx)
{
    /* com.palm.app.phone calls this when a call is picked up, to let audiod tell
     * a connected Bluetooth headset to stop ringing. Silencing the ringtone is
     * the part that matters locally -- and it has to actually silence it, which
     * means muting the ringtone stream rather than only noting it. */
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(OPTIONAL(client, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    LegacyCategory *ringtone = categoryByName("/ringtone");
    if (self && policy && ringtone && ringtone->streamType)
    {
        policy->setStreamMute(ringtone->streamType, true);
        self->notifyCategory(ringtone, "changed");
    }

    replySuccess(sh, message);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /dtmf
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_playDTMF(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    /* enyo's dialpad sends {"id":"5","oneshot":true,"feedbackOnly":false}, the
     * QML Phone app {"name":"5"} (with "feedbackOnly":true in a call); the
     * other two decide whether the tone also has to reach the far end. */
    LSMessageJsonParser msg(message, LegacyRequest::PLAY_DTMF);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Without id or name there is nothing to play: '{}' sent the network an
     * empty tone sequence during a call and answered success. */
    const std::optional<std::string> keys = LegacyRequest::dtmfKeys(msg.get());
    if (!keys || keys->empty())
    {
        replyInvalid(sh, message, "id (or name) must name the dialpad keys to play");
        return true;
    }
    const std::string id = *keys;
    const bool oneshot = LegacyRequest::boolean(msg.get(), "oneshot").value_or(true);
    const bool feedbackOnly = LegacyRequest::boolean(msg.get(), "feedbackOnly").value_or(false);

    CLSError lserror;
    for (const char ch : id)
    {
        if ((ch < '0' || ch > '9') && ch != '*' && ch != '#')
        {
            const char *reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                                    "id must be dialpad characters 0-9, * or #");
            if (!LSMessageReply(sh, message, reply, &lserror))
                lserror.Print(__FUNCTION__, __LINE__);
            return true;
        }
    }

    /* During a call the tone has to be generated by the network, or the far end
     * hears nothing and IVR menus cannot be driven. feedbackOnly says the
     * caller only wants the local beep. */
    if (self && self->mCallMode != eCallMode_None && !feedbackOnly)
    {
        LSHandle *audiod = self->legacyHandle();
        pbnjson::JValue payload = pbnjson::Object();
        const char *uri = nullptr;

        if (oneshot)
        {
            payload.put("toneSequence", id);
            uri = "luna://com.palm.telephony/sendDtmf";
        }
        else
        {
            payload.put("tone", id.substr(0, 1));
            uri = "luna://com.palm.telephony/dtmfStartLong";
        }

        const std::string body = payload.stringify();
        if (audiod && !LSCall(audiod, uri, body.c_str(), nullptr, nullptr, nullptr, &lserror))
        {
            lserror.Print(__FUNCTION__, __LINE__);
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "playDTMF: could not relay the tone to com.palm.telephony");
        }
    }

    /* Local feedback tone. audiod's generator plays a fixed-duration one-shot
     * that fades out on its own, so a missed release cannot leave a tone stuck
     * on. This generation's EVirtualSink has no eDTMF, so the keypad tone goes
     * through the feedback sink like the other UI sounds. */
    if (self && self->mObjAudioMixer && !id.empty())
        self->mObjAudioMixer->playOneshotDtmf(id.substr(0, 1).c_str(), efeedback);

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_stopDTMF(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;

    if (self && self->mCallMode != eCallMode_None)
    {
        LSHandle *audiod = self->legacyHandle();
        if (audiod && !LSCall(audiod, "luna://com.palm.telephony/dtmfEndLong", "{}",
                              nullptr, nullptr, nullptr, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }

    if (self && self->mObjAudioMixer)
        self->mObjAudioMixer->stopDtmf();

    replySuccess(sh, message);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /state
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_setRingerSwitch(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, LegacyRequest::RINGER_SWITCH);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* Not false for a ringtone that is not there: '{}' silenced the ringer. */
    const std::optional<bool> requested = LegacyRequest::boolean(msg.get(), "ringtone");
    if (!requested)
    {
        replyInvalid(sh, message, "ringtone must be true or false");
        return true;
    }
    const bool ringerOn = *requested;

    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    LegacyCategory *ringtone = categoryByName("/ringtone");
    if (self)
    {
        self->mRingerOn = ringerOn;
        /* A silenced ringer switch means the ringtone stream is muted -- that
         * is the audible half com.palm.app.clock's read of /system/status
         * assumes has happened. */
        if (policy && ringtone && ringtone->streamType)
            policy->setStreamMute(ringtone->streamType, !ringerOn);
        /* And, as on 2.x, alerts only lower the music with the switch on. */
        if (policy)
            policy->setRingerOn(ringerOn);
        /* With a USB headset, alerts ring on the loudspeaker too only with
         * the switch on. */
        self->syncUsbRing();
        self->notifyStatusSubscribers();
    }

    replySuccess(sh, message);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /systemsounds
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_playFeedback(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    /* Matches the Palm schema: enyo passes override and type on some paths and
     * an additionalProperties:false schema without them rejects the call. */
    LSMessageJsonParser msg(message, LegacyRequest::PLAY_FEEDBACK);
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    /* '{}' played the sound "" and answered success. */
    const std::optional<std::string> sound = LegacyRequest::string(msg.get(), "name");
    if (!sound || sound->empty())
    {
        replyInvalid(sh, message, "name must name a system sound");
        return true;
    }
    const std::string name = *sound;
    const std::string sinkName = LegacyRequest::string(msg.get(), "sink").value_or(std::string());
    const bool play = LegacyRequest::boolean(msg.get(), "play").value_or(true);

    if (play && self && self->mObjAudioMixer)
    {
        /* The Palm API lets a caller name the sink the feedback should go to;
         * honour it when it names a stream this generation still has, and fall
         * back to the feedback sink otherwise. */
        EVirtualAudioSink sink = efeedback;
        AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
        if (!sinkName.empty() && policy)
        {
            EVirtualAudioSink named = policy->sinkForStream(sinkName);
            if (named != eVirtualSink_None)
                sink = named;
            else
                PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                    "playFeedback: no sink named '%s' here; using the feedback sink",
                    sinkName.c_str());
        }
        self->mObjAudioMixer->playSystemSound(name.c_str(), sink);
    }

    replySuccess(sh, message);
    return true;
}
