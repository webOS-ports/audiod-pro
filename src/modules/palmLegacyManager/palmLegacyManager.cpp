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
#include "main.h"

#define PALM_AUDIO_SERVICE      "com.palm.audio"
#define PORTS_AUDIO_SERVICE     "org.webosports.service.audio"

/* Subscription keys. LSSubscriptionReply matches on an opaque string, so these
 * only have to agree with what the corresponding handler passes to
 * LSSubscriptionProcess. Using the "<category>/<method>" shape keeps them
 * readable in ls-monitor. */
#define KEY_ROOT_STATUS         "/getStatus"
#define KEY_PHONE_STATUS        "/phone/status"
#define KEY_RINGTONE_STATUS     "/ringtone/status"

bool PalmLegacyManager::mIsObjRegistered = PalmLegacyManager::RegisterObject();
PalmLegacyManager* PalmLegacyManager::mPalmLegacyManager = nullptr;

PalmLegacyManager* PalmLegacyManager::getPalmLegacyManagerInstance()
{
    return mPalmLegacyManager;
}

PalmLegacyManager::PalmLegacyManager(ModuleConfig* const pConfObj) :
    mObjAudioMixer(AudioMixer::getAudioMixerInstance()),
    mObjModuleManager(ModuleManager::getModuleManagerInstance()),
    mPalmHandle(nullptr),
    mPortsHandle(nullptr),
    mPaMainloop(nullptr),
    mPaContext(nullptr),
    mPaReady(false),
    mCallMode(eCallMode_None),
    mAppliedCallMode(eCallMode_None),
    mCallStatus(eCallStatus_None),
    mPhoneRoute(ePhoneRoute_Earpiece),
    mSpeakerMode(false),
    mPhoneMuted(false),
    mRingtoneMuted(false),
    mMicMuted(false),
    mHac(false),
    mVolumeLocked(false),
    mVolume(50),
    mMuted(false)
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
 * Method tables
 * ------------------------------------------------------------------------- */

LSMethod PalmLegacyManager::rootMethods[] = {
    { "getStatus",      PalmLegacyManager::_getStatus },
    { "setVolume",      PalmLegacyManager::_setVolume },
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
    { "status",                 PalmLegacyManager::_phoneStatus },
    { "setMuted",               PalmLegacyManager::_phoneSetMuted },
    { "CallStatusUpdate",       PalmLegacyManager::_callStatusUpdate },
    { "hacSet",                 PalmLegacyManager::_hacSet },
    { "hacGet",                 PalmLegacyManager::_hacGet },
    { "setCurrentScenario",     PalmLegacyManager::_setCurrentScenario },
    { "getCurrentScenario",     PalmLegacyManager::_getCurrentScenario },
    { "setVolume",              PalmLegacyManager::_genericSetVolume },
    { "getVolume",              PalmLegacyManager::_genericGetVolume },
    { },
};

LSMethod PalmLegacyManager::ringtoneMethods[] = {
    { "status",     PalmLegacyManager::_ringtoneStatus },
    { "setMuted",   PalmLegacyManager::_ringtoneSetMuted },
    { "setVolume",  PalmLegacyManager::_genericSetVolume },
    { "getVolume",  PalmLegacyManager::_genericGetVolume },
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
    { "status",         PalmLegacyManager::_genericStatus },
    { "setVolume",      PalmLegacyManager::_genericSetVolume },
    { "getVolume",      PalmLegacyManager::_genericGetVolume },
    { "lockVolumeKeys", PalmLegacyManager::_lockVolumeKeys },
    { },
};

LSMethod PalmLegacyManager::systemMethods[] = {
    { "status",     PalmLegacyManager::_genericStatus },
    { "setVolume",  PalmLegacyManager::_genericSetVolume },
    { "getVolume",  PalmLegacyManager::_genericGetVolume },
    { },
};

LSMethod PalmLegacyManager::vvmMethods[] = {
    { "status",     PalmLegacyManager::_genericStatus },
    { "control",    PalmLegacyManager::_vvmControl },
    { },
};

LSMethod PalmLegacyManager::systemsoundsMethods[] = {
    { "playFeedback",   PalmLegacyManager::_playFeedback },
    { },
};

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
        { "/vvm",           vvmMethods },
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

    if (pa_context_connect(mPaContext, nullptr, (pa_context_flags_t) PA_CONTEXT_NOFAIL, nullptr) < 0)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to connect to PulseAudio: %s", pa_strerror(pa_context_errno(mPaContext)));
        pa_context_unref(mPaContext);
        pa_glib_mainloop_free(mPaMainloop);
        mPaContext = nullptr;
        mPaMainloop = nullptr;
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
            /* A call may already have been set up while PulseAudio was still
             * coming up -- re-apply so the hardware matches our state. */
            if (self->mCallMode != eCallMode_None)
                self->applyCallRouting(self->mCallMode, self->mPhoneRoute);
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

bool PalmLegacyManager::applyCallRouting(ECallMode mode, EPhoneRoute route)
{
    if (!mPaReady || !mPaContext)
    {
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "applyCallRouting: PulseAudio is not ready, deferring");
        return false;
    }

    RoutingRequest *req = new(std::nothrow) RoutingRequest{this, mode, route};
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
        delete req;
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
        /* No card needed a profile change (or there were none): carry on to the
         * sinks directly. */
        pa_operation *op = pa_context_get_sink_info_list(c, &PalmLegacyManager::paSinkInfoCb, req);
        if (op)
            pa_operation_unref(op);
        else
            delete req;
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
                                                               req);
        if (op)
            pa_operation_unref(op);
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
        delete req;
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
            delete req;
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
            case ePhoneRoute_Headset:       preferred = headphones; break;
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
        /* End of the chain -- this is the one place the request is freed. */
        delete req;
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
    /* The bug this guards against, found by decompiling the webOS 3.0.5 audiod
     * and present in audiod-pro's State::setCallMode() too: the disconnect path
     * reset the software call mode but never pushed it down, so the device's
     * cached "last applied" mode stayed on the just-ended call. The next call
     * then set the same mode, this early-return matched, and the whole route
     * switch was skipped -- audio silently failed from the second call onwards.
     *
     * Comparing against mAppliedCallMode rather than a device-side cache keeps
     * the early return (it is still worth avoiding redundant hardware work)
     * while guaranteeing the disconnect transition is always applied, because
     * eCallMode_None differs from whatever the call was using. */
    if (mCallMode == mAppliedCallMode)
    {
        PM_LOG_DEBUG("updateCallMode: mode unchanged (%d), nothing to apply", (int) mCallMode);
        return;
    }

    if (applyCallRouting(mCallMode, mPhoneRoute))
        mAppliedCallMode = mCallMode;
    else
        PM_LOG_ERROR(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
            "Failed to apply call routing for mode %d; leaving applied mode at %d "
            "so the next transition retries", (int) mCallMode, (int) mAppliedCallMode);

    notifyStatusSubscribers();
}

/* ------------------------------------------------------------------------- *
 * Helpers
 * ------------------------------------------------------------------------- */

void PalmLegacyManager::postCategoryStatus(const char *category, const char *method,
                                           pbnjson::JValue reply)
{
    CLSError lserror;
    std::string key = std::string(category) + method;
    std::string payload = reply.stringify();

    if (mPalmHandle && !LSSubscriptionReply(mPalmHandle, key.c_str(), payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    if (mPortsHandle && !LSSubscriptionReply(mPortsHandle, key.c_str(), payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
}

void PalmLegacyManager::notifyStatusSubscribers()
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", mVolume);
    reply.put("mute", mMuted);
    reply.put("inCall", mCallMode != eCallMode_None);
    reply.put("speakerMode", mSpeakerMode);
    reply.put("micMute", mMicMuted);
    postCategoryStatus("", KEY_ROOT_STATUS, reply);

    pbnjson::JValue phoneReply = pbnjson::Object();
    phoneReply.put("returnValue", true);
    phoneReply.put("muted", mPhoneMuted);
    phoneReply.put("hac", mHac);
    phoneReply.put("inCall", mCallMode != eCallMode_None);
    phoneReply.put("scenario", mSpeakerMode ? "phone_back_speaker" : "phone_front_speaker");
    postCategoryStatus("", KEY_PHONE_STATUS, phoneReply);
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

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    if (self)
    {
        reply.put("volume", self->mVolume);
        reply.put("mute", self->mMuted);
        reply.put("inCall", self->mCallMode != eCallMode_None);
        reply.put("speakerMode", self->mSpeakerMode);
        reply.put("micMute", self->mMicMuted);
    }
    if (subscribed)
        reply.put("subscribed", true);

    std::string payload = reply.stringify();
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_setVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(volume, integer)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    int volume = 0;
    msg.get("volume", volume);

    std::string reply = STANDARD_JSON_SUCCESS;
    if (volume < 0 || volume > 100)
    {
        reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_PARAMS,
                                    "Volume out of range. Must be in [0;100]");
    }
    else if (self)
    {
        self->mVolume = volume;
        self->notifyStatusSubscribers();
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, reply.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_setMute(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(OPTIONAL(mute, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool mute = false;
    msg.get("mute", mute);

    if (self)
    {
        self->mMuted = mute;
        self->notifyStatusSubscribers();
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
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
        self->notifyStatusSubscribers();
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
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
        bool inCall = (self->mCallMode != eCallMode_None);
        bool speakerMode = self->mSpeakerMode;
        msg.get("inCall", inCall);
        msg.get("speakerMode", speakerMode);

        self->mSpeakerMode = speakerMode;
        self->mPhoneRoute = speakerMode ? ePhoneRoute_Speaker : ePhoneRoute_Earpiece;
        self->mCallMode = inCall ? eCallMode_Carrier : eCallMode_None;
        self->mCallStatus = inCall ? eCallStatus_Active : eCallStatus_Disconnected;
        self->updateCallMode();
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_volumeUp(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    if (self)
    {
        self->mVolume = (self->mVolume >= 100) ? 100 : self->mVolume + 1;
        self->notifyStatusSubscribers();
    }
    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_volumeDown(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    if (self)
    {
        self->mVolume = (self->mVolume <= 0) ? 0 : self->mVolume - 1;
        self->notifyStatusSubscribers();
    }
    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /phone
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_phoneStatus(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    if (self)
    {
        reply.put("muted", self->mPhoneMuted);
        reply.put("hac", self->mHac);
        reply.put("inCall", self->mCallMode != eCallMode_None);
        reply.put("scenario", self->mSpeakerMode ? "phone_back_speaker" : "phone_front_speaker");
    }
    if (subscribed)
        reply.put("subscribed", true);

    std::string payload = reply.stringify();
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_phoneSetMuted(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(OPTIONAL(muted, boolean)));
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
        self->notifyStatusSubscribers();
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_callStatusUpdate(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_2(OPTIONAL(transport, string),
                                              OPTIONAL(status, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string transport;
    std::string status;
    msg.get("transport", transport);
    msg.get("status", status);

    if (self)
    {
        if (status == "active")
            self->mCallStatus = eCallStatus_Active;
        else if (status == "disconnected")
            self->mCallStatus = eCallStatus_Disconnected;
        else if (status == "incoming")
            self->mCallStatus = eCallStatus_Incoming;
        else if (status == "dialing")
            self->mCallStatus = eCallStatus_Dialing;

        if (self->mCallStatus == eCallStatus_Active)
        {
            /* com.palm.telephony means a carrier call through the modem's voice
             * path; anything else (our IM/VoIP connectors) stays on PCM. */
            self->mCallMode = (transport == "com.palm.telephony") ? eCallMode_Carrier
                                                                  : eCallMode_Voip;
            self->updateCallMode();
        }
        else if (self->mCallStatus == eCallStatus_Disconnected)
        {
            self->mCallMode = eCallMode_None;
            /* Deliberately called on this path too -- see updateCallMode(). */
            self->updateCallMode();
        }
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_hacSet(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(OPTIONAL(hac, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool hac = false;
    msg.get("hac", hac);
    if (self)
    {
        self->mHac = hac;
        self->notifyStatusSubscribers();
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_hacGet(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("hac", self ? self->mHac : false);

    std::string payload = reply.stringify();
    CLSError lserror;
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_setCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(scenario, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string scenario;
    msg.get("scenario", scenario);

    if (self)
    {
        if (scenario == "phone_back_speaker")
        {
            self->mPhoneRoute = ePhoneRoute_Speaker;
            self->mSpeakerMode = true;
        }
        else if (scenario == "phone_front_speaker")
        {
            self->mPhoneRoute = ePhoneRoute_Earpiece;
            self->mSpeakerMode = false;
        }
        else if (scenario == "phone_headset" || scenario == "phone_headset_mic")
        {
            self->mPhoneRoute = ePhoneRoute_Headset;
            self->mSpeakerMode = false;
        }
        else if (scenario == "phone_bluetooth_sco")
        {
            self->mPhoneRoute = ePhoneRoute_BluetoothSCO;
            self->mSpeakerMode = false;
        }

        /* The decompiled 3.0.5 audiod dropped routing changes outright unless
         * the call was already Active, which silently lost every scenario switch
         * made while a call was still ringing or dialing. Apply it whenever a
         * call mode is set, and let updateCallMode() pick it up on the Active
         * transition otherwise. */
        if (self->mCallMode != eCallMode_None)
        {
            self->applyCallRouting(self->mCallMode, self->mPhoneRoute);
            self->notifyStatusSubscribers();
        }
    }

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_getCurrentScenario(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    const char *scenario = "phone_front_speaker";
    if (self)
    {
        switch (self->mPhoneRoute)
        {
            case ePhoneRoute_Speaker:       scenario = "phone_back_speaker"; break;
            case ePhoneRoute_Headset:       scenario = "phone_headset"; break;
            case ePhoneRoute_BluetoothSCO:  scenario = "phone_bluetooth_sco"; break;
            case ePhoneRoute_Earpiece:
            default:                        scenario = "phone_front_speaker"; break;
        }
    }

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("scenario", scenario);

    std::string payload = reply.stringify();
    CLSError lserror;
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /ringtone
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_ringtoneSetMuted(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_1(OPTIONAL(muted, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    bool muted = false;
    msg.get("muted", muted);
    if (self)
        self->mRingtoneMuted = muted;

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_ringtoneStatus(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("muted", self ? self->mRingtoneMuted : false);
    if (subscribed)
        reply.put("subscribed", true);

    std::string payload = reply.stringify();
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /telephony
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_telephonyAnswered(LSHandle *sh, LSMessage *message, void *ctx)
{
    /* com.palm.app.phone calls this when a call is picked up, to let audiod tell
     * a connected Bluetooth headset to stop ringing. Muting the ringtone is the
     * part that matters locally. */
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    if (self)
        self->mRingtoneMuted = true;

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /dtmf
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_playDTMF(LSHandle *sh, LSMessage *message, void *ctx)
{
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(name, string)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string name;
    msg.get("name", name);

    /* Tone synthesis has not been ported from the Palm-era tonegenerator yet,
     * so the dialpad is silent rather than wrong. Kept as a real method so
     * com.palm.app.phone's Dialpad gets a success rather than a bus error. */
    PM_LOG_INFO(MSGID_PALM_LEGACY_MANAGER, INIT_KVCOUNT,
        "playDTMF(%s): tone generation not yet implemented", name.c_str());

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_stopDTMF(LSHandle *sh, LSMessage *message, void *ctx)
{
    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /media, /system, /vvm
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_genericStatus(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    CLSError lserror;
    bool subscribed = false;

    if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", self ? self->mVolume : 0);
    reply.put("muted", self ? self->mMuted : false);
    if (subscribed)
        reply.put("subscribed", true);

    std::string payload = reply.stringify();
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_genericSetVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    return _setVolume(sh, message, ctx);
}

bool PalmLegacyManager::_genericGetVolume(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("volume", self ? self->mVolume : 0);

    std::string payload = reply.stringify();
    CLSError lserror;
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
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

    std::string payload = reply.stringify();
    if (!LSMessageReply(sh, message, payload.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

bool PalmLegacyManager::_vvmControl(LSHandle *sh, LSMessage *message, void *ctx)
{
    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}

/* ------------------------------------------------------------------------- *
 * /systemsounds
 * ------------------------------------------------------------------------- */

bool PalmLegacyManager::_playFeedback(LSHandle *sh, LSMessage *message, void *ctx)
{
    PalmLegacyManager *self = getPalmLegacyManagerInstance();
    LSMessageJsonParser msg(message, SCHEMA_3(REQUIRED(name, string),
                                              OPTIONAL(sink, string),
                                              OPTIONAL(play, boolean)));
    if (!msg.parse(__FUNCTION__, sh))
        return true;

    std::string name;
    bool play = true;
    msg.get("name", name);
    msg.get("play", play);

    if (play && self && self->mObjAudioMixer)
        self->mObjAudioMixer->playSystemSound(name.c_str(), efeedback);

    CLSError lserror;
    if (!LSMessageReply(sh, message, STANDARD_JSON_SUCCESS, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}
