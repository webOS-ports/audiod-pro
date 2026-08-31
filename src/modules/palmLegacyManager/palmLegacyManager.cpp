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

#include "palmLegacyManager.h"
#include "audioPolicyManager.h"
#include "main.h"

#define PALM_AUDIO_SERVICE      "com.palm.audio"
#define PORTS_AUDIO_SERVICE     "org.webosports.service.audio"

#define MASTER_GET_VOLUME       "luna://com.webos.service.audio/master/getVolume"
#define MASTER_URI_PREFIX       "luna://com.webos.service.audio/master/"

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
    mCarrierStatus(eCallStatus_NoCall),
    mVoipStatus(eCallStatus_NoCall),
    mCallWithVideo(false),
    mPhoneMuted(false),
    mMicMuted(false),
    mHac(false),
    mVolumeLocked(false),
    mRingerOn(true),
    mActiveSoundOutput(""),
    mMasterVolume(0),
    mMasterMuted(false),
    mPaMainloop(nullptr),
    mPaContext(nullptr),
    mPaReady(false)
{
    if (!mObjAudioMixer)
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT, "AudioMixer instance is null");
    PM_LOG_DEBUG("palmLegacyManager constructor");
}

PalmLegacyManager::~PalmLegacyManager()
{
    PM_LOG_DEBUG("palmLegacyManager destructor");
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
std::vector<std::string> PalmLegacyManager::scenariosFor(const LegacyCategory *cat)
{
    std::vector<std::string> list;
    if (!cat || !cat->scenarioPrefix)
        return list;

    const std::string p(cat->scenarioPrefix);
    list.push_back(p + "_front_speaker");
    list.push_back(p + "_back_speaker");
    list.push_back(p + "_headset");
    list.push_back(p + "_headset_mic");
    if (0 == strcmp(cat->scenarioPrefix, "media"))
    {
        list.push_back(p + "_a2dp");
        list.push_back(p + "_wireless");
    }
    else
    {
        list.push_back(p + "_bluetooth_sco");
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

    /* Mirror the master volume so the root category can serve it without
     * caching a number of its own. */
    subscribeMasterVolume();

    /* The active output device is what master/setVolume has to be addressed
     * at, and audioRouter is the thing that knows it. */
    if (mObjModuleManager)
        mObjModuleManager->subscribeModuleEvent(this, utils::eEventActiveDeviceInfo);

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

    if (mPaContext)
    {
        pa_context_set_state_callback(mPaContext, nullptr, nullptr);
        pa_context_disconnect(mPaContext);
        pa_context_unref(mPaContext);
        mPaContext = nullptr;
    }
    if (mPaMainloop)
    {
        pa_glib_mainloop_free(mPaMainloop);
        mPaMainloop = nullptr;
    }
    mPaReady = false;

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

    switch (ev->eventName)
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
            }
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
    char name[64];

    mPaMainloop = pa_glib_mainloop_new(GetMainLoopContext());
    if (!mPaMainloop)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not create a PulseAudio glib mainloop");
        return false;
    }

    snprintf(name, sizeof(name), "PalmLegacyManager:%i", getpid());
    mPaContext = pa_context_new(pa_glib_mainloop_get_api(mPaMainloop), name);
    if (!mPaContext)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not create a PulseAudio context");
        pa_glib_mainloop_free(mPaMainloop);
        mPaMainloop = nullptr;
        return false;
    }

    pa_context_set_state_callback(mPaContext, &PalmLegacyManager::paContextStateCb, this);

    if (pa_context_connect(mPaContext, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Could not connect to PulseAudio: %s",
            pa_strerror(pa_context_errno(mPaContext)));
        return false;
    }

    return true;
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
            PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Connected to PulseAudio for call routing");
            /* Assert routing for our current state so the hardware matches
             * audiod's view whenever we (re)connect. PulseAudio outlives an
             * audiod restart and keeps whatever card profile / sink port the
             * previous instance left -- which can be a bare 'voicecall' profile
             * or a parked output port, giving no sound. Applying even the idle
             * (eCallMode_None) routing here restores the default profile and a
             * real output port (speaker/earpiece); it is idempotent, so it is a
             * no-op when the hardware is already correct. This also re-pushes an
             * in-progress call's routing if PulseAudio came up mid-call. */
            {
                LegacyCategory *phone = categoryByName("/phone");
                self->applyCallRouting(self->mCallMode,
                                       phone ? phone->route : ePhoneRoute_Earpiece);
            }
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            /* PA_CONTEXT_NOFAIL means libpulse reconnects on its own; drop the
             * applied mode so the next transition is re-pushed to the hardware
             * rather than being swallowed by updateCallMode()'s early return. */
            self->mPaReady = false;
            self->mAppliedCallMode = eCallMode_None;
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "Lost the PulseAudio connection; call routing is suspended");
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

    RoutingRequest *req = new(std::nothrow) RoutingRequest{this, mode, route, 1};
    if (!req)
        return false;

    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "applyCallRouting: mode=%d route=%d", (int) mode, (int) route);

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
         * the explicit mode-1 variant when present, as the Palm-era audiod did. */
        if (!strcasecmp(p->name, "voicecall-voicemmode1"))
            voiceCall = p;
        else if (!voiceCall && (!strcasecmp(p->name, "voicecall") ||
                                !strcasecmp(p->name, "voice call") ||
                                !strcasecmp(p->name, "Voice Call")))
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

    pa_sink_port_info *earpiece = nullptr, *speaker = nullptr, *headphones = nullptr;
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
        else if ((!strcmp(p->name, "output-wired_headset") ||
                  !strcmp(p->name, "output-wired_headphone")) &&
                 p->available != PA_PORT_AVAILABLE_NO)
            headphones = p;
    }

    /* An earpiece is what identifies the handset's own sink; anything without
     * one is some other output (HDMI, USB, a loopback) and is left alone. */
    if (!earpiece)
        return;

    if (req->mode != eCallMode_None)
    {
        switch (req->route)
        {
            case ePhoneRoute_Speaker:       preferred = speaker; break;
            case ePhoneRoute_Headset:
            case ePhoneRoute_HeadsetMic:    preferred = headphones; break;
            case ePhoneRoute_BluetoothSCO:  preferred = nullptr; break;  /* handled by the BT card */
            case ePhoneRoute_Earpiece:
            default:                        preferred = headphones ? headphones : earpiece; break;
        }
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

    pa_source_port_info *builtinMic = nullptr, *headsetMic = nullptr, *preferred = nullptr;

    for (uint32_t i = 0; i < info->n_ports; i++)
    {
        pa_source_port_info *p = info->ports[i];
        if (!p)
            continue;

        if (!strcmp(p->name, "input-builtin_mic"))
            builtinMic = p;
        else if (!strcmp(p->name, "input-wired_headset") && p->available != PA_PORT_AVAILABLE_NO)
            headsetMic = p;
    }

    if (!builtinMic)
        return;     /* Not the handset's own capture source. */

    preferred = headsetMic ? headsetMic : builtinMic;

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

void PalmLegacyManager::updateCallMode()
{
    LegacyCategory *phone = categoryByName("/phone");
    const EPhoneRoute route = phone ? phone->route : ePhoneRoute_Earpiece;

    /* The bug this guards against, found by decompiling the webOS 3.0.5 audiod
     * and present in audiod-pro's State::setCallMode() too: the disconnect path
     * reset the software call mode but never pushed it down, so the device's
     * cached "last applied" mode stayed on the just-ended call. The next call
     * then set the same mode, this early-return matched, and the whole route
     * switch was skipped -- audio silently failed from the second call onwards.
     *
     * The route is part of the comparison as well, or a speakerphone toggle
     * during a call (which changes the route but not the mode) would be
     * swallowed here and never reach the hardware. */
    if (mCallMode == mAppliedCallMode && route == mAppliedRoute)
    {
        PM_LOG_DEBUG("updateCallMode: mode %d / route %d unchanged, nothing to apply",
            (int) mCallMode, (int) route);
        return;
    }

    if (applyCallRouting(mCallMode, route))
    {
        mAppliedCallMode = mCallMode;
        mAppliedRoute = route;
    }
    else
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to apply call routing for mode %d; leaving applied mode at %d "
            "so the next transition retries", (int) mCallMode, (int) mAppliedCallMode);
    }

    notifyStatusSubscribers();
}

/* A carrier call outranks a VoIP one: if the modem is up, that is what the
 * hardware has to be routed for. */
PalmLegacyManager::ECallStatus PalmLegacyManager::effectiveCallStatus() const
{
    auto live = [](ECallStatus s) {
        return s == eCallStatus_Active || s == eCallStatus_OnHold ||
               s == eCallStatus_Incoming || s == eCallStatus_Dialing ||
               s == eCallStatus_Connecting;
    };

    if (live(mCarrierStatus))
        return mCarrierStatus;
    if (live(mVoipStatus))
        return mVoipStatus;
    return eCallStatus_Disconnected;
}

void PalmLegacyManager::applyCallStatus()
{
    const bool carrierActive = (mCarrierStatus == eCallStatus_Active ||
                                mCarrierStatus == eCallStatus_OnHold);
    const bool voipActive = (mVoipStatus == eCallStatus_Active ||
                             mVoipStatus == eCallStatus_OnHold);

    if (carrierActive)
        mCallMode = eCallMode_Carrier;
    else if (voipActive)
        mCallMode = eCallMode_Voip;
    else
        mCallMode = eCallMode_None;

    /* Deliberately called on the disconnect path too -- see updateCallMode(). */
    updateCallMode();
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

bool PalmLegacyManager::_rootSetVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_2(REQUIRED(volume, integer),
                                              OPTIONAL(scenario, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    int volume = 0;
    msg.get("volume", volume);

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
        self->applyCallRouting(self->mCallMode,
                               categoryByName("/phone") ? categoryByName("/phone")->route
                                                        : ePhoneRoute_Earpiece);
        self->notifyStatusSubscribers();
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_setCallMode(LSHandle *sh, LSMessage *message, void *ctx)
{
    /* The org.webosports.service.audio spelling used by the QML phone app and
     * kept for anything still calling it. phone/CallStatusUpdate is the richer
     * path and the one com.palm.app.phone uses. */
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(inCall, boolean),
                                              OPTIONAL(speakerMode, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    if (self)
    {
        LegacyCategory *phone = categoryByName("/phone");
        bool inCall = (self->mCallMode != eCallMode_None);
        bool speakerMode = phone && phone->route == ePhoneRoute_Speaker;
        msg.get("inCall", inCall);
        msg.get("speakerMode", speakerMode);

        if (phone)
            phone->route = speakerMode ? ePhoneRoute_Speaker : ePhoneRoute_Earpiece;

        self->mCarrierStatus = inCall ? eCallStatus_Active : eCallStatus_Disconnected;
        self->mVoipStatus = eCallStatus_Disconnected;
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
    LSMessageJsonParser msg(message, SCHEMA_2(REQUIRED(volume, integer),
                                              OPTIONAL(scenario, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    int volume = 0;
    msg.get("volume", volume);

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
    LSMessageJsonParser msg(message, SCHEMA_3(REQUIRED(offset, integer),
                                              OPTIONAL(scenario, string),
                                              OPTIONAL(unit, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    int offset = 0;
    msg.get("offset", offset);

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
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(muted, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool muted = false;
    msg.get("muted", muted);

    AudioPolicyManager *policy = AudioPolicyManager::getAudioPolicyManagerInstance();
    if (self && cat && cat->streamType && policy)
    {
        policy->setStreamMute(cat->streamType, muted);
        self->notifyCategory(cat, "changed");
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_listScenarios(LSHandle *sh, LSMessage *message, void *ctx)
{
    LegacyCategory *cat = categoryFor(message);
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(enabled, boolean),
                                              OPTIONAL(disabled, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    pbnjson::JValue scenarios = pbnjson::Array();
    for (const auto &name : scenariosFor(cat))
        scenarios.append(name);

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
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(scenario, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string scenario;
    msg.get("scenario", scenario);

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

    cat->route = route;

    if (0 == strcmp(cat->category, "/phone"))
    {
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
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(muted, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool muted = false;
    msg.get("muted", muted);

    if (self)
    {
        self->mPhoneMuted = muted;
        /* On Palm hardware muting the call muted the capture path, not the
         * playback one -- the far end stops hearing you, you keep hearing them. */
        self->mMicMuted = muted;
        LegacyCategory *phone = categoryByName("/phone");
        self->applyCallRouting(self->mCallMode,
                               phone ? phone->route : ePhoneRoute_Earpiece);
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
 */
bool PalmLegacyManager::_callStatusUpdate(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(lines, array)));
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
        replySuccess(sh, message);
        return true;
    }

    auto statusFromString = [](const std::string &s) {
        if (s == "active")      return eCallStatus_Active;
        if (s == "incoming")    return eCallStatus_Incoming;
        if (s == "connecting")  return eCallStatus_Connecting;
        if (s == "dialing")     return eCallStatus_Dialing;
        if (s == "onHold")      return eCallStatus_OnHold;
        if (s == "disconnected") return eCallStatus_Disconnected;
        return eCallStatus_NoCall;
    };

    /* "No calls at all" is reported as an empty array rather than a line in
     * state "disconnected". Without this the loop below never runs, the call
     * mode is never reset, and every getOnActiveCall()-gated behaviour stays
     * stuck on the last call for the life of the process. Found on real
     * hardware during the TouchPad port; carried over verbatim. */
    if (0 == lines.arraySize())
    {
        self->mCarrierStatus = eCallStatus_Disconnected;
        self->mVoipStatus = eCallStatus_Disconnected;
        self->mCallWithVideo = false;
        self->applyCallStatus();
        replySuccess(sh, message);
        return true;
    }

    bool sawCarrier = false, sawVoip = false;
    bool withVideo = false;

    for (ssize_t i = 0; i < lines.arraySize(); i++)
    {
        std::string state;
        std::string transport;

        if (lines[i]["state"].asString(state) != CONV_OK)
        {
            PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
                "CallStatusUpdate: line %zd has no state", (ssize_t) i);
            continue;
        }

        pbnjson::JValue calls = lines[i]["calls"];
        if (calls.isArray() && calls.arraySize() > 0)
            calls[0]["transport"].asString(transport);

        const ECallStatus status = statusFromString(state);

        /* com.palm.telephony is the carrier stack; anything else (our IM/VoIP
         * connectors) stays on the normal PCM path. */
        if (transport == "com.palm.telephony")
        {
            sawCarrier = true;
            self->mCarrierStatus = status;
        }
        else
        {
            sawVoip = true;
            self->mVoipStatus = status;

            bool outgoingVideo = false, incomingVideo = false;
            lines[i]["outgoingVideo"].asBool(outgoingVideo);
            lines[i]["incomingVideo"].asBool(incomingVideo);
            if (outgoingVideo || incomingVideo)
                withVideo = true;
        }
    }

    if (!sawCarrier)
        self->mCarrierStatus = eCallStatus_Disconnected;
    if (!sawVoip)
        self->mVoipStatus = eCallStatus_Disconnected;
    self->mCallWithVideo = withVideo;

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

    if (self)
    {
        self->mHac = hac;
        self->notifyStatusSubscribers();
    }

    replySuccess(sh, message);
    return true;
}

bool PalmLegacyManager::_hacGet(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("hac", self ? self->mHac : false);
    replyJson(sh, message, reply);
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
    /* The dialpad sends {"id":"5","oneshot":true,"feedbackOnly":false} -- "id",
     * not "name", and the other two decide whether the tone also has to reach
     * the far end. */
    LSMessageJsonParser msg(message, SCHEMA_4(REQUIRED(id, string),
                                              OPTIONAL(oneshot, boolean),
                                              OPTIONAL(feedbackOnly, boolean),
                                              OPTIONAL(name, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string id;
    bool oneshot = true;
    bool feedbackOnly = false;

    if (!msg.get("id", id))
        msg.get("name", id);
    if (!msg.get("oneshot", oneshot))
        oneshot = true;
    msg.get("feedbackOnly", feedbackOnly);

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
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(ringtone, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool ringerOn = true;
    msg.get("ringtone", ringerOn);

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
    LSMessageJsonParser msg(message, SCHEMA_5(REQUIRED(name, string),
                                              OPTIONAL(sink, string),
                                              OPTIONAL(play, boolean),
                                              OPTIONAL(override, boolean),
                                              OPTIONAL(type, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string name;
    std::string sinkName;
    bool play = true;
    msg.get("name", name);
    msg.get("sink", sinkName);
    if (!msg.get("play", play))
        play = true;

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
