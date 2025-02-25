// Copyright (c) 2012-2019 LG Electronics, Inc.
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


#include <lunaservice.h>
#include <cstring>

#include "state.h"
#include "utils.h"
#include "messageUtils.h"
#include "module.h"
#include "phone.h"
#include "media.h"
#include "voicecommand.h"
#include "vvm.h"
#include "update.h"
#include "AudioDevice.h"
#include "AudioMixer.h"
#include "system.h"
#include "ringtone.h"
#include "log.h"
#include "main.h"
#include "../include/IPC_SharedAudiodProperties.cpp"
#include "AudiodCallbacks.h"
#include "VolumeControlChangesMonitor.h"
#include "vibrate.h"
#include "scenario.h"
#include "timer.h"
#include "alert.h"
#include "genericScenarioModule.h"
#include <pulse/simple.h>


#define DEFAULT_CARRIER_BUSYTONE_REPEATS 5
#define DEFAULT_CARRIER_EMERGENCYTONE_REPEATS 3
State gState;
GlobalConf gGlobalConf;
bool callbackReceived = true;

/*
 * Generic preference system for boolean & string preferences
 * This is designed to provide a consistent & robust way to create new preferences
 * that behave like all the others:
 * - automatic setters & getters
 * - automatic persistance
 * - automatic support for getPreference/setPreference API
 * To create a new preference:
 * - create a new name, below, exposed in header
 * - initialize the value in State::State()
 * - use it where needed! (never use the text definition to avoid typos)
 */

const char * cPref_VibrateWhenRingerOn = "VibrateWhenRingerOn";
const char * cPref_VibrateWhenRingerOff = "VibrateWhenRingerOff";
const char * cPref_VoiceCommandSupportWhenSecureLocked = "VoiceCommandSupportWhenSecureLocked";
const char * cPref_TextEntryCorrectionHapticPolicy = "TextEntryCorrectionHapticPolicy";
const char * cPref_BeatsOnForHeadphones = "BeatsOnForHeadphones";
const char * cPref_BeatsOnForSpeakers = "BeatsOnForSpeakers";
const char * cPref_RingerOn = "RingerOn";
const char * cPref_DndOn = "DndOn";
const char * cPref_PrevRingerOn = "PrevRingerOn";
const char * cPref_PrevVibrateWhenRingerOn = "PrevVibrateWhenRingerOn";
const char * cPref_PrevVibrateWhenRingerOff = "PrevVibrateWhenRingerOff";
const char * cPref_TouchOn = "TouchOn";
const char * cPref_OverrideRingerForAlaram = "AlarmOn";
const char * cPref_OverrideRingerForTimer = "TimerOn";
const char * cPref_VolumeBalanceOnHeadphones = "VolumeBalance";
const char * cPref_RingtoneWithVibration = "RingtonewithVibrationWhenEnabled";


class PhoneCallHandler
{
public:
    PhoneCallHandler()
    {
        IPC_SharedAudiodProperties::getInstance()->mPhoneStatus.sendChanges
                (boost::bind(&PhoneCallHandler::phoneStatusChanged, this, _1));
    }

    void    phoneStatusChanged(const EPhoneStatus & phoneStatus)
    {
        const char * status = "??";
        switch (phoneStatus)
        {
        case ePhoneStatus_Disconnected:    status = "disconnected";    break;
        case ePhoneStatus_Dialing:        status = "dialing";            break;
        case ePhoneStatus_Incoming:        status = "incoming";        break;
        case ePhoneStatus_Connected:    status = "connected";        break;
        default:                        status = "undefined";
        }
        struct tm localTime;
        time_t now = ::time(0);
        ::localtime_r(&now, &localTime);
        g_message("Phone call status: %s at %02d:%02d:%02d", status,
                                                             localTime.tm_hour,
                                                             localTime.tm_min,
                                                             localTime.tm_sec);

        if ((phoneStatus == ePhoneStatus_Incoming &&
             gAudiodProperties->mRingerOn.get())
                || phoneStatus == ePhoneStatus_Dialing
                || phoneStatus == ePhoneStatus_Connected)
        {
            gState.setActiveSinksAtPhoneCallStart(gAudioMixer.getActiveStreams());
            gState.pauseAllMediaSaved();
        }
        else if (phoneStatus == ePhoneStatus_Disconnected)
        {
            gState.resumeAllMediaSaved();
            gState.setActiveSinksAtPhoneCallStart(VirtualSinkSet());
        }
    }
};

static  void onDisplayOnChanged(const bool & newValue)
{
    struct tm localTime;
    time_t now = ::time(0);
    ::localtime_r(&now, &localTime);
    g_message("Display is %s at %02d:%02d:%02d", newValue ? "on" :
                                                            "off",
                                                             localTime.tm_hour,
                                                             localTime.tm_min,
                                                             localTime.tm_sec);
}

class SetAudiodCommandBehavior : public  IPC_SetPropertyBehavior<EAudiodCommand>
{
    virtual bool    set(IPC_ServerProperty<EAudiodCommand> & property,
                                                const EAudiodCommand & cmd)
    {
        switch (cmd)
        {
        case eAudiodCommand_Null:
            break;

        case eAudiodCommand_UnmuteMedia:
            g_debug("Audiod Cmd: Unmuting media, if any!");
            getMediaModule()->programMediaVolumes(false, true, false);
            break;

        default:
            g_warning("Unhandled audiod command %d", cmd);
        }
        return true;
    }
};



State::State()
{
    mOnActiveCall = false;
    mCarrier = false;
    mVoip = false;
    mNumVoip = 0;
    mCallMode = eCallMode_None;
    mCallWithVideo = false;
    mPauseAllMediaSaved = false;
    mOnThePuck = false;
    mSliderState = eSlider_Closed;
    mTTYMode = eTTYMode_Off;
    mHAC = false;
    mLockedVolumeModule = 0;
    mUseUdevForHeadsetEvents = false;
    mIncomingCallActive = false;
    mIncomingCarrierCallActive = false;
    mIncomingVoipCallActive = false;
    mBTServerRunning = false;
    mAdjustMicGain = false;
    mInputStreamActive = false;
    mMediaserverIsRecording = false;
    mForceMicOn = false;
    mPhoneSecureLockActive = true;
    mBTHfpConnected = false;
    mBallance = 0;
    mQvoiceOpened = false;
    mRecordOpened = false;
    mLoopback = false;
    mRTPLoaded = false;

    // declare & initialize supported preferences to default values.
    // No other preference are supported.
    mBooleanPreferences[cPref_VibrateWhenRingerOn].init(false);
    mBooleanPreferences[cPref_VibrateWhenRingerOff].init(true);
    mStringPreferences[cPref_VoiceCommandSupportWhenSecureLocked].init("off");
    mStringPreferences[cPref_TextEntryCorrectionHapticPolicy].init("auto");
    mBooleanPreferences[cPref_BeatsOnForHeadphones].init(true);
    mBooleanPreferences[cPref_BeatsOnForSpeakers].init(false);
    mBooleanPreferences[cPref_RingerOn].init(false);
    mBooleanPreferences[cPref_DndOn].init(false);
    mBooleanPreferences[cPref_PrevRingerOn].init(false);
    mBooleanPreferences[cPref_PrevVibrateWhenRingerOff].init(true);
    mBooleanPreferences[cPref_PrevVibrateWhenRingerOn].init(false);
    mBooleanPreferences[cPref_TouchOn].init(false);

    mBooleanPreferences[cPref_OverrideRingerForAlaram].init(true);
    mBooleanPreferences[cPref_OverrideRingerForTimer].init(true);
    mIntegerPreferences[cPref_VolumeBalanceOnHeadphones].init(0);
    mBooleanPreferences[cPref_RingtoneWithVibration].init(true);

}

void State::init()
{
    IPC_SharedAudiodProperties::getInstance();

    new PhoneCallHandler();
    gAudiodProperties->mDisplayOn.sendChanges(&onDisplayOnChanged);
    gAudiodProperties->mAudiodCmd.setSetPropertyBehavior(new SetAudiodCommandBehavior());
    gAudiodProperties->mRecordingAudio.sendChanges(boost::
                          bind(&State::setMediaServerIsRecording, &gState, _1));
    try
    {
        gAudiodProperties->mRingerOn.set(false);
    }
    catch(...)
    {
        g_debug("Exception occured [State::init()]");
    }
}

bool State::setLockedVolumeModule (ScenarioModule *module)
{
    if (module && mLockedVolumeModule)
        return false;

    if (module)
    {
        g_debug ("%s: locking volume module '%s'.", __FUNCTION__,
                                                        module->getCategory());
    }
    else
    {
        if (mLockedVolumeModule)
            g_debug ("%s: unlocking volume module '%s'.",
                             __FUNCTION__, mLockedVolumeModule->getCategory());
    }

    mLockedVolumeModule = module;

    return true;
}

// determines if a module controls the volume by some explicit request
GenericScenarioModule * State::getExplicitVolumeControlModule ()
{
    if (mLockedVolumeModule)
        return mLockedVolumeModule;

    GenericScenarioModule * currentModule = GenericScenarioModule::getCurrent();

    if (currentModule && currentModule->getVolumeOverride() > 0)
        return currentModule;

    return 0;
}

// determines which module controls the volume
GenericScenarioModule * State::getCurrentVolumeModule ()
{
    GenericScenarioModule * controllingModule = State::getExplicitVolumeControlModule();
    if (controllingModule)
        return controllingModule;

    return GenericScenarioModule::getCurrent();
}

bool State::getScoUp ()
{
    return mScoUp;
}

void State::setScoUp (bool state)
{
    mScoUp = state;
}

bool State::getTabletConnected ()
{
    return mTabletConnected;
}

void State::setTabletConnected (bool state)
{
    mTabletConnected = state;
}

bool State::getOnActiveCall ()
{
    return mOnActiveCall;
}

#if defined(MACHINE_BROADWAY)
static int firstCall = 1;
#endif

void State::setOnActiveCall (bool state, ECallMode mode)
{
    int totalState = 0;

    if (mode == eCallMode_Carrier) {
        if (mCarrier == state)
            return;
        mCarrier = state;
    } else if (mode == eCallMode_Voip) {
        if (mVoip == state)
            return;
        mVoip = state;
    }

    totalState = mVoip | mCarrier;

    if (mOnActiveCall != totalState)
    {
        mOnActiveCall = totalState;

        ScenarioModule *phone = getPhoneModule();
        ScenarioModule *media = getMediaModule();

#if defined(MACHINE_BROADWAY)
        const char *currentPhoneScenario = phone->getCurrentScenarioName();
#endif
        if (getVoiceCommandModule()->isCurrentModule() && mOnActiveCall) {
            // fire the event to voicecommand but don't change the module
            // until the voicecommand module quit!
            // see VoiceCommandModule::endVoiceCommand()
            getVoiceCommandModule()->stopVoiceCommand("audiod phone call");
            getVvmModule()->stopVvm("audiod phone call");
        } else
        if (mOnActiveCall) {
            phone->makeCurrent();
#if defined(MACHINE_BROADWAY)
            // Take out this hack once we figure out why this is necessary to
            // get uplink/downlink audio working on the first call
            if (firstCall == 1) {
                Scenario * tempScenario;
                g_warning("first call on phone, doing this hack to get   \
                       uplink/downlink audio working until modem fixes issue");
                phone->enableScenario (cPhone_BackSpeaker);
                tempScenario = phone->getScenario(cPhone_BackSpeaker.c_str());
                gAudioDevice.setRouting(tempScenario->mName, eRouting_base, 0);

                phone->enableScenario (currentPhoneScenario);
                tempScenario = phone->getScenario(currentPhoneScenario);
                gAudioDevice.setRouting(tempScenario->mName, eRouting_base, 0);
                firstCall = 0;
            }
#endif
        }
        else
        {
            media->makeCurrent();

            // since the call ended reset the scenario to
            // the one that should be selected by priority
            phone->setCurrentScenarioByPriority();
            // if phone connected to tablet, unset bluetooth sco scenario
            if (gState.getTabletConnected())
                phone->unsetCurrentScenario (cPhone_BluetoothSCO);
        }
    }
}

EPhoneStatus State::getPhoneStatus ()
{
    return gAudiodProperties->mPhoneStatus.get();
}

void State::setPhoneStatus (EPhoneStatus state)
{
    gAudiodProperties->mPhoneStatus.set(state);
}

void State::setDisplayOn (bool on)
{
    gAudiodProperties->mDisplayOn.set(on);
}

bool State::getDisplayOn ()
{
    return gAudiodProperties->mDisplayOn.get();
}

bool State::getRingerOn ()
{
    return gAudiodProperties->mRingerOn.get();
}

void State::setLoopbackStatus (bool status)
{
    gState.mLoopback = status;
}

bool State::getLoopbackStatus ()
{
    return gState.mLoopback;
}

bool State::checkPhoneScenario (ScenarioModule * phone)
{
    bool isEnabled = false;

    if(nullptr != phone)
    {
        GenericScenario * tempScenario = phone->getScenario(cPhone_BackSpeaker);
        if(nullptr !=tempScenario)
        {
             isEnabled = tempScenario->mEnabled;
        }
    }

    return isEnabled;
}

void State::setRingerOn (bool ringerOn)
{
    if (getRingerOn() != ringerOn)
    {
        gState.setPreference(cPref_RingerOn, ringerOn);
        gAudiodProperties->mRingerOn.set(ringerOn);
        if (ScenarioModule * module = dynamic_cast <ScenarioModule *> (ScenarioModule::getCurrent()))
            module->programSoftwareMixer(true);

        ScenarioModule *phone   = getPhoneModule();
        ScenarioModule *media   = getMediaModule();
        ScenarioModule *system  = getSystemModule();
        ScenarioModule *ringtone = getRingtoneModule();
        ScenarioModule *timer = getTimerModule();
        ScenarioModule *alert = getAlertModule();

        CHECK(phone->sendChangedUpdate(UPDATE_CHANGED_RINGER));
        CHECK(media->sendChangedUpdate(UPDATE_CHANGED_RINGER));
        CHECK(ringtone->sendChangedUpdate(UPDATE_CHANGED_RINGER));
        CHECK(system->sendChangedUpdate(UPDATE_CHANGED_RINGER));
        CHECK(timer->sendChangedUpdate(UPDATE_CHANGED_RINGER));
        CHECK(alert->sendChangedUpdate(UPDATE_CHANGED_RINGER));

        if (mBooleanPreferences[cPref_VibrateWhenRingerOff].mValue &&
                                            !ringerOn && !ringtone->isMuted())
        {
            // if on an active call,
            // these scenarios should not vibrate, so just return
            if (getOnActiveCall())
            {
                const char * currentScenarioName =
                                    getPhoneModule()->getCurrentScenarioName();
                if (cPhone_FrontSpeaker == currentScenarioName ||
                    cPhone_BackSpeaker == currentScenarioName ||
                    cPhone_Headset == currentScenarioName)
                {
                    return;
                }
            }

            if (getOnThePuck () || getOnActiveCall ())
            {
                gAudioMixer.playSystemSound ("alert_buzz", eeffects);
            }
            else
            {
                //VibrateDevice::realVibrate (false);
                getVibrateDevice()->realVibrate(false);
            }
        }
    }
}

void State::setActiveInputStream (bool active)
{
    mInputStreamActive = active;
    updateIsRecording();
}

void State::setMediaServerIsRecording (const bool & recording)
{
    mMediaserverIsRecording = recording;
    updateIsRecording();
}

void State::setForceMicOn (bool force)
{
    mForceMicOn = force;
    updateIsRecording();

    g_message("State::setForceMicOn: %s (mic is %s)", force ?
                                                      "true" :
                                                      "false",
                                                      isRecording() ? "on" : "off");
}

static
gboolean _updateIsRecordingCallback (gpointer data)
{
    gState.updateIsRecording();
    return FALSE;
}

void  State::updateIsRecording()
{
    static guint64    sKeepRecordingUntil = 0;

    bool shouldRecord = isRecording();

    guint64    now = getCurrentTimeInMs();
    if (shouldRecord != gAudioDevice.isRecording())
    {
        if (!shouldRecord && sKeepRecordingUntil == 0)
        {
            sKeepRecordingUntil = now + 1000;
            g_timeout_add_full(G_PRIORITY_HIGH_IDLE, 1000,
                                    _updateIsRecordingCallback, NULL, NULL);
        }
        else if (shouldRecord || now >= sKeepRecordingUntil)
        {
            gAudioDevice.setRecording(shouldRecord);
            sKeepRecordingUntil = 0;
        }
    }else
        sKeepRecordingUntil = 0;
}

bool State::isRecording ()
{
    return mInputStreamActive || mMediaserverIsRecording || mForceMicOn;
}

void State::setTTYMode (ETTYMode mode)
{

    if (mTTYMode == mode)
        return;

    switch (mode) {
        case eTTYMode_Full:
            g_debug ("%s: setting ttyMode to FULL", __FUNCTION__);
            mTTYMode = mode;
            break;
        case eTTYMode_HCO:
            g_debug ("%s: setting ttyMode to HCO", __FUNCTION__);
            mTTYMode = mode;
            break;
        case eTTYMode_VCO:
            g_debug ("%s: setting ttyMode to VCO", __FUNCTION__);
            mTTYMode = mode;
            break;
        case eTTYMode_Off:
            g_debug ("%s: setting ttyMode to OFF", __FUNCTION__);
            mTTYMode = mode;
            break;
        default:
            g_warning ("%s: unknown tty mode, setting ttyMode to OFF", __FUNCTION__);
            mTTYMode = eTTYMode_Off;
            break;
    }

    if (getHeadsetState() == eHeadsetState_Headset ||
        getHeadsetState() == eHeadsetState_HeadsetMic)
    {
        ScenarioModule *phone = getPhoneModule();

        if (eTTYMode_Full == mTTYMode)
        {
            phone->enableScenario (cPhone_TTY_Full);
            phone->disableScenario (cPhone_TTY_HCO);
            phone->disableScenario (cPhone_TTY_VCO);
        }
        else if (eTTYMode_HCO == mTTYMode)
        {
            phone->enableScenario (cPhone_TTY_HCO);
            phone->disableScenario (cPhone_TTY_Full);
            phone->disableScenario (cPhone_TTY_VCO);
        }
        else if (eTTYMode_VCO == mTTYMode)
        {
            phone->enableScenario (cPhone_TTY_VCO);
            phone->disableScenario (cPhone_TTY_Full);
            phone->disableScenario (cPhone_TTY_HCO);
        }
        else
        {
            phone->disableScenario (cPhone_TTY_Full);
            phone->disableScenario (cPhone_TTY_HCO);
            phone->disableScenario (cPhone_TTY_VCO);
        }
        phone->setCurrentScenarioByPriority ();
    }
}

void State::hacSet (bool mode)
{
    if ((mHAC && mode) || (!mHAC && !mode)) return;
    mHAC = mode;
    ScenarioModule *phone = getPhoneModule();

    gAudioDevice.hacSet(mHAC);
    if (mHAC)
    {
        phone->disableScenario (cPhone_BackSpeaker);
        phone->setCurrentScenario (cPhone_FrontSpeaker);
    }
    else
    {
        phone->enableScenario (cPhone_BackSpeaker);
    }
}

ETTYMode State::getTTYMode ()
{
    return mTTYMode;
}

ECallMode State::getCallMode ()
{
    return mCallMode;
}

void State::setCallMode (ECallMode mode, ECallStatus status)
{
    ScenarioModule * phone = getPhoneModule();
    MediaScenarioModule * media = getMediaModule();

    switch (mode) {
        case eCallMode_Voip:
            if (status == eCallStatus_Incoming) {
                g_debug("incoming voip call");
                mNumVoip++;
                if (!gState.getOnActiveCall ()) {
                    media->broadcastEvent ("call:incoming");    // tell luna & Cie ASAP
                    gState.setIncomingCallActive (true, eCallMode_Voip);

                    if (gState.getRingerOn ())
                        media->rampDownAndMute();

                    gState.setPhoneStatus(ePhoneStatus_Incoming);
                } else {
                    g_debug("call mode is voip and incoming call is carrier, so play busy tone");
                    gAudioDevice.phoneEvent(ePhoneEvent_IncomingCallTone, 0);
                }
            } else if (status == eCallStatus_Active) {
                g_debug("active voip call");
                g_warning("%s: %d: Call mode is voip!", __FUNCTION__, __LINE__);
                mCallMode = eCallMode_Voip;
                gAudioDevice.updateCallMode();
                if (!gState.getOnActiveCall ()) {
                    media->rampDownAndMute();

                    gState.setPhoneStatus(ePhoneStatus_Connected);
                    media->broadcastEvent("call:connected");

                    gState.setIncomingCallActive (false, eCallMode_Voip);
                    gState.setOnActiveCall (true, eCallMode_Voip);

                    if (phone && cPhone_BluetoothSCO == phone->getCurrentScenarioName())
                    {
                        bool isEnabled = checkPhoneScenario(phone);
                        // Note: we set it on back/front here,
                        // _btHfgSubscription() may set it to
                        // phone_bluetooth_sco later when
                        // user press bluetooth button
                        if (gState.getOnThePuck() && isEnabled) {
                            phone->setCurrentScenario (cPhone_BackSpeaker);
                        } else {
                            phone->setCurrentScenario (cPhone_FrontSpeaker);
                        }
                    }
                }
                phone->programMuted();
                gAudioDevice.setIncomingCallRinging(false);
            } else if (status == eCallStatus_Dialing) {
                g_debug("dialing voip call");
                mNumVoip++;
                if (!gState.getOnActiveCall ()) {
                    gState.setIncomingCallActive (true, eCallMode_Voip);

                    media->rampDownAndMute();

                    gState.setPhoneStatus(ePhoneStatus_Dialing);
                    media->broadcastEvent ("call:dialing");
                    if (phone && cPhone_BluetoothSCO == phone->getCurrentScenarioName())
                    {
                        bool isEnabled = checkPhoneScenario(phone);
                        // Note: we set it on back/front here,
                        // _btHfgSubscription() may set it to
                        // phone_bluetooth_sco later when
                        //user press bluetooth button
                        if (gState.getOnThePuck() && isEnabled) {
                            phone->setCurrentScenario (cPhone_BackSpeaker);
                        } else {
                            phone->setCurrentScenario (cPhone_FrontSpeaker);
                        }
                    }
                    gState.setOnActiveCall (true, eCallMode_Voip);
                }
            } else if (status == eCallStatus_Disconnected) {
                g_debug("disconnected voip call");
                mNumVoip--;

                // some reason, this becomes negative,
                // probably due to bad phone app messages.
                if (mNumVoip < 0)
                    mNumVoip = 0;

                if (mNumVoip == 0) {
                    if (gState.getOnActiveCall() || gState.getIncomingCallActive()) {
                        gState.setIncomingCallActive(false, eCallMode_Voip);
                        gState.setOnActiveCall(false, eCallMode_Voip);
                    }

                    if (!gState.getOnActiveCall() || !gState.getIncomingCallActive()) {
                        getMediaModule()->setMuted (false);
                        getMediaModule()->programMediaVolumes(true, true);
                        gState.setPhoneStatus(ePhoneStatus_Disconnected);

                        // for the next call
                        getRingtoneModule()->setMuted (false);
                        getMediaModule()->broadcastEvent("call:disconnected");

                        // in case we did not stop streaming because ringer switch
                        // was off we need to notify A2DP to resume streaming
                        // if the media streams are still playing.
                        getMediaModule()->resumeA2DP();
                    }

                    if (mCarrier) {
                        g_debug("%s: %d: Call mode is carrier!", __FUNCTION__, __LINE__);
                        mCallMode = eCallMode_Carrier;
                    }
                    else {
                        g_debug("%s: %d: Call mode is none!", __FUNCTION__, __LINE__);
                        mCallMode = eCallMode_None;
                    }
                    setCallWithVideo(false);
                    phone->programMuted();
                }
            }
            break;

        case eCallMode_Carrier:
            if (status == eCallStatus_Active) {
                g_debug("%s: %d: Call mode is carrier!", __FUNCTION__, __LINE__);
                mCallMode = eCallMode_Carrier;
                gAudioDevice.updateCallMode();
            } else if (status == eCallStatus_Disconnected) {
                if (mVoip) {
                    g_debug("%s: %d: Call mode is voip!", __FUNCTION__, __LINE__);
                    mCallMode = eCallMode_Voip;
                }
                else {
                    g_debug("%s: %d: Call mode is none!", __FUNCTION__, __LINE__);
                    mCallMode = eCallMode_None;
                }
            }
            phone->programMuted();

            break;

        default :
        case eCallMode_None:
            break;
    }

}

void State::setCallWithVideo (bool callWithVideo)
{
    if (mCallWithVideo!=callWithVideo) {
        ScenarioModule *phone = getPhoneModule();
        mCallWithVideo = callWithVideo;
        if (mCallWithVideo) {
            g_debug("%s Disable frontspeaker", __PRETTY_FUNCTION__);
            phone->disableScenario (cPhone_FrontSpeaker);
        } else {
            g_debug("%s Enable frontspeaker", __PRETTY_FUNCTION__);
            phone->enableScenario (cPhone_FrontSpeaker);
        }
    }
}

bool State::getCallWithVideo()
{
    return mCallWithVideo;
}

void State::setSliderState (ESliderState state)
{
    if (mSliderState != state)
    {
        bool update = false;
        if (mSliderState == eSlider_Open ||
            (mSliderState != eSlider_Open && state == eSlider_Open))
            update = true;

        mSliderState = state;

        if (update) {
            ScenarioModule *phone   = getPhoneModule();
            ScenarioModule *media   = getMediaModule();
            ScenarioModule *system  = getSystemModule();
            ScenarioModule *ringtone = getRingtoneModule();
            ScenarioModule *timer =  getTimerModule();
            ScenarioModule *alert = getAlertModule();

            CHECK(phone->sendChangedUpdate(UPDATE_CHANGED_SLIDER));
            CHECK(media->sendChangedUpdate(UPDATE_CHANGED_SLIDER));
            CHECK(system->sendChangedUpdate(UPDATE_CHANGED_SLIDER));
            CHECK(ringtone->sendChangedUpdate(UPDATE_CHANGED_SLIDER));
            CHECK(timer->sendChangedUpdate(UPDATE_CHANGED_SLIDER));
            CHECK(alert->sendChangedUpdate(UPDATE_CHANGED_SLIDER));
        }
    }
}

ESliderState State::getSliderState ()
{
    return mSliderState;
}

EHeadsetState State::getHeadsetState ()
{
    return gAudiodProperties->mHeadsetState.get();
}

void State::setHeadsetRoute (EHeadsetState newState)
{
    bool ret = false;

    if (newState == eHeadsetState_Headset) {
        ret = gAudioMixer.programHeadsetRoute(1);
    }
    else if (newState == eHeadsetState_None) {
        ret = gAudioMixer.programHeadsetRoute(0);
    }
    if (ret == false)
        g_debug("Failed execution of programHeadsetRoute");
}

bool State::setMicOrHeadset (EHeadsetState state, int cardno, int deviceno, int status)
{
    bool ret = false;

    if (state == eHeadsetState_UsbMic_Connected || state == eHeadsetState_UsbMic_DisConnected)
        ret = gAudioMixer.loadUSBSinkSource('j',cardno,deviceno,status);
    else if (state == eHeadsetState_UsbHeadset_Connected || state == eHeadsetState_UsbHeadset_DisConnected)
        ret = gAudioMixer.loadUSBSinkSource('z',cardno,deviceno,status);
    else
        return false;

    if (false == ret) {
        g_debug("Failed execution of loadUSBSinkSource");
        return false;
    }
    return true;
}

static const char * _getHeadsetStateName(EHeadsetState state)
{
    if (state == eHeadsetState_Headset)
        return "Headset";
    else if (state == eHeadsetState_HeadsetMic)
        return "Headset Mic";
    else if (state == eHeadsetState_None)
        return "<None>";
    return "<invalid>";
}

void State::setHeadsetState (EHeadsetState newState)
{
    EHeadsetState previousState = getHeadsetState();

    if (previousState == newState)
        return;

    g_debug("%s: %s", __FUNCTION__, _getHeadsetStateName(newState));

    setHeadsetRoute(newState);

    gAudiodProperties->mHeadsetState.set(newState);
    gAudioDevice.setHeadsetState(newState);

    ScenarioModule *phone = getPhoneModule();
    ScenarioModule *media = getMediaModule();
    ScenarioModule *voice = getVoiceCommandModule();
    ScenarioModule *vvm = getVvmModule();

    if (eHeadsetState_None == newState)
    {
        if (eHeadsetState_HeadsetMic == previousState)
        {
            phone->disableScenario (cPhone_HeadsetMic);
            media->disableScenario (cMedia_HeadsetMic);
            voice->disableScenario (cVoiceCommand_HeadsetMic);
            vvm->disableScenario (cVvm_HeadsetMic);
        }
        else if (eHeadsetState_Headset == previousState)
        {
            phone->disableScenario (cPhone_Headset);
            media->disableScenario (cMedia_Headset);
            voice->disableScenario (cVoiceCommand_Headset);
            vvm->disableScenario (cVvm_Headset);
        }
        if (mTTYMode != eTTYMode_Off)
        {
            phone->disableScenario (cPhone_TTY_Full);
            phone->disableScenario (cPhone_TTY_HCO);
            phone->disableScenario (cPhone_TTY_VCO);
        }
    }
    else if (eHeadsetState_HeadsetMic == newState)
    {
        phone->enableScenario (cPhone_HeadsetMic);
        media->enableScenario (cMedia_HeadsetMic);
        voice->enableScenario (cVoiceCommand_HeadsetMic);
        vvm->enableScenario (cVvm_HeadsetMic);
        if (mTTYMode != eTTYMode_Off)
        {
            switch (mTTYMode) {
                case eTTYMode_Full:
                    phone->enableScenario (cPhone_TTY_Full);
                    break;
                case eTTYMode_HCO:
                    phone->enableScenario (cPhone_TTY_HCO);
                    break;
                case eTTYMode_VCO:
                    phone->enableScenario (cPhone_TTY_VCO);
                    break;
                default:
                    break;
            }
        }

        phone->setCurrentScenarioByPriority ();
        media->setCurrentScenarioByPriority ();
        voice->setCurrentScenarioByPriority ();
        vvm->setCurrentScenarioByPriority ();

        if (eHeadsetState_Headset == previousState)
        {
            phone->disableScenario (cPhone_Headset);
            media->disableScenario (cMedia_Headset);
            voice->disableScenario (cVoiceCommand_Headset);
            vvm->disableScenario (cVvm_Headset);
        }
    }
    else if (eHeadsetState_Headset == newState)
    {
        phone->enableScenario (cPhone_Headset);
        media->enableScenario (cMedia_Headset);
        voice->enableScenario (cVoiceCommand_Headset);
        vvm->enableScenario (cVvm_Headset);
        if (mTTYMode != eTTYMode_Off)
        {
            switch (mTTYMode) {
                case eTTYMode_Full:
                    phone->enableScenario (cPhone_TTY_Full);
                    break;
                case eTTYMode_HCO:
                    phone->enableScenario (cPhone_TTY_HCO);
                    break;
                case eTTYMode_VCO:
                    phone->enableScenario (cPhone_TTY_VCO);
                    break;
                default:
                    break;
            }
        }
        phone->setCurrentScenarioByPriority ();
        media->setCurrentScenarioByPriority ();
        voice->setCurrentScenarioByPriority ();
        vvm->setCurrentScenarioByPriority ();

        if (eHeadsetState_HeadsetMic == previousState)
        {
            phone->disableScenario (cPhone_HeadsetMic);
            media->disableScenario (cMedia_HeadsetMic);
            voice->disableScenario (cVoiceCommand_HeadsetMic);
            vvm->disableScenario (cVvm_HeadsetMic);
        }
    }

    if (newState != eHeadsetState_None)
    {
        // wake up the display
        CLSError lserror;
        bool result;

        LSHandle * sh = GetPalmService();
        result = LSCall (sh, "palm://com.palm.display/control/setState",
                              "{\"state\":\"on\"}", NULL, NULL, NULL, &lserror);
        if (!result)
            lserror.Print(__FUNCTION__, __LINE__);
    }
}


bool State::getPreference(const char * name, bool & outValue)
{
    TBooleanPreferences::iterator pref = mBooleanPreferences.find(name);
    if (pref == mBooleanPreferences.end())
    {
        g_warning("State::getBooleanPreference: '%s'   \
                               is not a supported boolean preference", name);
        return false;
    }
    outValue = pref->second.mValue;
    return true;
}

bool State::setPreference(const char * name, bool value)
{
    TBooleanPreferences::iterator pref = mBooleanPreferences.find(name);
    if (pref == mBooleanPreferences.end())
    {
        g_warning("State::setBooleanPreference: '%s'   \
                                 is not a supported boolean preference", name);
        return false;
    }
    if (pref->second.mValue != value)
    {
        pref->second.mValue = value;
        storePreferences();
    }
    return true;
}

bool State::setPreference(const char * name, int balance)
{
    TintPreferences::iterator pref = mIntegerPreferences.find(name);
    if (pref == mIntegerPreferences.end())
    {
        g_warning("State::setInteferPreference: '%s'   \
                                 is not a supported integer preference", name);
        return false;
    }
    if (pref->second.mValue != balance)
    {
        pref->second.mValue = balance;
        storePreferences();
    }
    return true;
}

bool State::getPreference(const char * name, int & outValue)
{
     TintPreferences::iterator pref = mIntegerPreferences.find(name);
    if (pref == mIntegerPreferences.end())
    {
        g_warning("State::getStringPreference: '%s'   \
                                  is not a supported string preference", name);
        return false;
    }
    outValue = pref->second.mValue;
    return true;
}

bool State::getPreference(const char * name, std::string & outValue)
{
    TStringPreferences::iterator pref = mStringPreferences.find(name);
    if (pref == mStringPreferences.end())
    {
        g_warning("State::getStringPreference: '%s'   \
                                  is not a supported string preference", name);
        return false;
    }
    outValue = pref->second.mValue;
    return true;
}

bool State::setPreference(const char * name, const std::string & value)
{
    TStringPreferences::iterator pref = mStringPreferences.find(name);
    if (pref == mStringPreferences.end())
    {
        g_warning("State::setStringPreference: '%s'  \
                                 is not a supported string preference", name);
        return false;
    }
    if (pref->second.mValue != value)
    {
        pref->second.mValue = value;
        storePreferences();
    }
    return true;
}

bool State::shouldVibrate()
{
    bool    vibrate = true;
    CHECK(getPreference(getRingerOn() ?
          cPref_VibrateWhenRingerOn : cPref_VibrateWhenRingerOff, vibrate));
    return vibrate;
}

bool State::getOnThePuck ()
{
    return mOnThePuck;
}

void State::setOnThePuck (bool state)
{
    mOnThePuck = state;
    // in case we are vibrating and we just got put on the cradle,
    // stop vibrating
    if (mOnThePuck)
       cancelVibrate ();
}

void State::setPhoneLocked (bool state)
{
    gAudiodProperties->mPhoneLocked.set(state);
}

bool State::getPhoneLocked ()
{
    return gAudiodProperties->mPhoneLocked.get();
}

void State::setIncomingCallActive (bool state, ECallMode mode)
{
    if (mode == eCallMode_Carrier) {
        if (mIncomingCarrierCallActive == state)
            return;
        mIncomingCarrierCallActive = state;
    } else if (mode == eCallMode_Voip) {
        if (mIncomingVoipCallActive == state)
            return;
        mIncomingVoipCallActive = state;
    }

    mIncomingCallActive = mIncomingCarrierCallActive | mIncomingVoipCallActive;

    if (getVoiceCommandModule()->isCurrentModule() && mIncomingCallActive) {
        // fire the event to voicecommand but don't change the module
        // until the voicecommand module quit!
        // see VoiceCommandModule::endVoiceCommand()
        getVoiceCommandModule()->stopVoiceCommand("audiod phone incoming call");
    } else if (getVvmModule()->isCurrentModule() && mIncomingCallActive) {
        getVvmModule()->stopVvm("audiod phone incoming call");
    } 
}

bool State::getIncomingCallActive ()
{
    return mIncomingCallActive;
}

void State::setActiveSinksAtPhoneCallStart (VirtualSinkSet sinks)
{
    mActiveSinksAtPhoneCallStart = sinks;
}

VirtualSinkSet State::getActiveSinksAtPhoneCallStart ()
{
    return mActiveSinksAtPhoneCallStart;
}

void State::setBTServerRunning (bool state)
{
    if (mBTServerRunning != state)
    {
        mBTServerRunning = state;
        VolumeControlChangesMonitor::mediaModuleControllingVolumeChanged();
    }
}

void State::setHfpStatus(bool state)
{
     if (mBTHfpConnected != state)
         mBTHfpConnected = state;
}

bool State::getHfpStatus ()
{
    return mBTHfpConnected;
}

void State::setQvoiceStatus(bool state)
{
     if (mQvoiceOpened != state)
         mQvoiceOpened = state;
}

bool State::getQvoiceStatus ()
{
    return mQvoiceOpened;
}

void State::setRecordStatus(bool state)
{
     if (mRecordOpened != state)
         mRecordOpened = state;
}

bool State::getRecordStatus ()
{
    return mRecordOpened;
}

void State::btOpenSCO(LSHandle *lshandle)
{
    if (!mScoUp) {
        CLSError lserror;
        if (!LSCall(lshandle, "luna://com.palm.bluetooth/hfp/opensco", "{}", NULL, NULL, NULL, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        else{
            g_message ("State::btOpenSCO: open sco sucess");
            mScoUp = true;
        }
    }
}

void State::btCloseSCO(LSHandle * lshandle)
{
    if (mScoUp) {
        CLSError lserror;
        if (!LSCall(lshandle, "luna://com.palm.bluetooth/hfp/closesco", "{}", NULL, NULL, NULL, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
        else{
            g_message ("State::btCloseSCO: close sco sucess");
            mScoUp = false;
        }
    }
}

void State::pauseAllMedia()
{
    gAudiodProperties->mMediaServerCmd.set(eMediaServerCommand_PauseAllMedia);
}

void State::pauseAllMediaSaved()
{
    if (!mPauseAllMediaSaved) {
        mPauseAllMediaSaved = true;
        gAudiodProperties->mMediaServerCmd.set(eMediaServerCommand_PauseAllMediaSaved);
    }
}

void State::resumeAllMediaSaved()
{
    if (mPauseAllMediaSaved) {
        gAudiodProperties->mMediaServerCmd.set(eMediaServerCommand_ResumeAllMediaSaved);
        mPauseAllMediaSaved = false;
    }
}


void State::umiMixerInit(GMainLoop *loop, LSHandle *handle)
{
  mObjUmiMixerInstance = umiaudiomixer::getUmiMixerInstance();
  if (nullptr!=mObjUmiMixerInstance)
  {
    mObjUmiMixerInstance->initUmiMixer(loop, handle, &gAudiodCallbacks);
  }
  else
  {
    g_message ("State:  m_ObjUmiMixerInstance in null");
  }
}

int
ControlInterfaceInit(GMainLoop *loop, LSHandle *handle)
{
    gAudioMixer.init(loop, handle, &gAudiodCallbacks);
    gState.umiMixerInit(loop, handle);
    if (ScenarioModule * module = dynamic_cast <ScenarioModule*> (GenericScenarioModule::getCurrent()))
        module->programHardwareState ();

    return 0;    }
    
static void
cancelSubscriptionCallback(LSMessage * message, LSMessageJsonParser & msgParser)
{
    if (ConstString(cModuleMethod_LockVolumeKeys) == LSMessageGetMethod(message))
    {
        VolumeControlChangesMonitor    monitor;

        // this a foreground app thingy
        bool foregroundApp;
        if (msgParser.get("foregroundApp", foregroundApp) && foregroundApp)
        {
            ScenarioModule * media =  getMediaModule();
            if (media->mCategory == LSMessageGetCategory(message))
            {
                media->setVolumeOverride(false);
            }
        }

        if (!foregroundApp)
            gState.setLockedVolumeModule (NULL);
    }
}

static int
StateInit (void)
{
    registerCancelSubscriptionCallback(&cancelSubscriptionCallback);

    // Recover variables stored
    gState.restorePreferences();

    return 0;
}

#if defined(AUDIOD_PALM_LEGACY)
static bool
_setState(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    LSMessageJsonParser    msg(message,
                               SCHEMA_2(OPTIONAL(adjustMicGain, boolean),
                                         OPTIONAL(forceMicOn, boolean)));
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    const char * reply = STANDARD_JSON_ERROR(2, "no setting to change found");

    bool adjustMicGain;
    if (msg.get("adjustMicGain", adjustMicGain))
    {
        gState.setAdjustMicGain(adjustMicGain);
        reply = STANDARD_JSON_SUCCESS;
    }

    bool forceMicOn;
    if (msg.get("forceMicOn", forceMicOn))
    {
        gState.setForceMicOn(forceMicOn);
        reply = STANDARD_JSON_SUCCESS;
    }

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}
#endif

bool State::getTouchSound() {
    bool touchSound;
    gState.getPreference(cPref_TouchOn, touchSound);
    return touchSound;
}

void State::setTouchSound(bool touchSound)
{
    if(getTouchSound() != touchSound)
    {
        g_message ("Setting TouchSound\n");
        gAudiodProperties->mTouchOn.set(touchSound);
        gState.setPreference(cPref_TouchOn, touchSound);
    }
}

void State::storeSoundProfile()
{
    bool ringerOn = false;
    bool vibrateWhenRingerOn = false;
    bool vibrateWhenRingerOff = false;

    gState.getPreference(cPref_RingerOn, ringerOn);
    gState.getPreference(cPref_VibrateWhenRingerOn, vibrateWhenRingerOn);
    gState.getPreference(cPref_VibrateWhenRingerOff, vibrateWhenRingerOff);

    gState.setPreference(cPref_PrevRingerOn, ringerOn);
    gState.setPreference(cPref_PrevVibrateWhenRingerOn, vibrateWhenRingerOn);
    gState.setPreference(cPref_PrevVibrateWhenRingerOff, vibrateWhenRingerOff);
}

void State::retrieveSoundProfile()
{
    bool ringerOn = false;
    bool vibrateWhenRingerOn = false;
    bool vibrateWhenRingerOff = false;

    gState.getPreference(cPref_PrevRingerOn, ringerOn);
    gState.getPreference(cPref_PrevVibrateWhenRingerOn, vibrateWhenRingerOn);
    gState.getPreference(cPref_PrevVibrateWhenRingerOff, vibrateWhenRingerOff);

    gState.setRingerOn(ringerOn);
    gState.setPreference(cPref_VibrateWhenRingerOn, vibrateWhenRingerOn);
    gState.setPreference(cPref_VibrateWhenRingerOff, vibrateWhenRingerOff);
}

void State::setDndMode(bool dndEnable)
{
    bool dndOn = false;

    gState.getPreference(cPref_DndOn, dndOn);

    if (dndOn == dndEnable)
        return;

    gState.setPreference(cPref_DndOn, dndEnable);

    if (dndEnable) {
        gState.storeSoundProfile();

        g_debug("Setting Sound Profile to Silent");
        gState.setRingerOn(false);
        gState.setPreference(cPref_VibrateWhenRingerOn, false);
        gState.setPreference(cPref_VibrateWhenRingerOff, false);
    }
    else
        gState.retrieveSoundProfile();

    CHECK(gState.sendUpdatedProfile(GetPalmService(),NULL));
}

static bool
_getTouchSound(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    LSMessageJsonParser    msg(message,SCHEMA_0);
    pbnjson::JValue    reply = pbnjson::Object();
    reply.put("returnValue", true);
    const std::string name = "touchSound";
    bool touchOn = gState.getTouchSound();

    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    if (touchOn)
        reply.put(name.c_str(), "ON");
    else
        reply.put(name.c_str(), "OFF");

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, jsonToString(reply).c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}

static bool
_setTouchSound(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    std::string touch;
    const char * reply = STANDARD_JSON_SUCCESS;
    LSMessageJsonParser msg(message, SCHEMA_1(REQUIRED(touch, string)));

    if (!msg.parse(__FUNCTION__, lshandle)) {
        reply = STANDARD_JSON_ERROR(3, "touch sound setting failed: missing parameters ?");
        goto error;
    }

    if (!msg.get("touch", touch)) {
        reply = MISSING_PARAMETER_ERROR(touch, string);
        goto error;
    } else {
        if((touch == "ON") || (touch == "on"))
            gState.setTouchSound(true);
        else if ((touch == "OFF") || (touch == "off"))
            gState.setTouchSound(false);
    }

    error:
    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}

static bool
_setRingerSwitch(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    LSMessageJsonParser    msg(message,
                               SCHEMA_1(REQUIRED(ringer, boolean)));
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    const char * reply = STANDARD_JSON_ERROR(2, "no setting to change found");

    bool ringer;
    if (msg.get("ringer", ringer))
    {
        gState.setRingerOn(ringer);
        reply = STANDARD_JSON_SUCCESS;
    }

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}


static bool
_setSoundProfile(LSHandle *lshandle, LSMessage *message, void *ctx)
{

     std::string ringer ;
     std::string vibrate;
     const char * reply = STANDARD_JSON_SUCCESS;

     LSMessageJsonParser    msg(message,
                                        SCHEMA_2(REQUIRED(ringer, string),
                                                           REQUIRED(vibrate, string)));

    if (!msg.parse(__FUNCTION__, lshandle)){
       reply = STANDARD_JSON_ERROR(3, "profile setting failed: missing parameters ?");
       goto error;
    }

    if (!msg.get("ringer", ringer)) {
       reply = MISSING_PARAMETER_ERROR(ringer, string);
       goto error;
    }

    if (!msg.get("vibrate", vibrate)) {
        reply = MISSING_PARAMETER_ERROR(vibrate, string);
        goto error;
    }

    if ((ringer == "ON") || (ringer == "on")){
        gState.setRingerOn(true);
        if ((vibrate =="ON") || (vibrate == "on")){
            /* sound + vibrate */
            gState.setPreference(cPref_VibrateWhenRingerOn, true);
            gState.setPreference(cPref_VibrateWhenRingerOff, false);
        }
        else if ((vibrate =="OFF") || (vibrate == "off")){
            /* only sound */
            gState.setPreference(cPref_VibrateWhenRingerOn, false);
            gState.setPreference(cPref_VibrateWhenRingerOff, false);
        }
        else{
            reply = STANDARD_JSON_ERROR(3, "profile setting failed: Invalid value for vibrate");
            goto error;
        }
    }
    else if ((ringer =="OFF") || (ringer == "off")){
        gState.setRingerOn(false);
        if ((vibrate =="ON") || (vibrate == "on")){
            /* only vibrate */
            gState.setPreference(cPref_VibrateWhenRingerOn, false);
            gState.setPreference(cPref_VibrateWhenRingerOff, true);
        }
        else if ((vibrate =="OFF") || (vibrate == "off")){
            /* silent */
            gState.setPreference(cPref_VibrateWhenRingerOn, false);
            gState.setPreference(cPref_VibrateWhenRingerOff, false);
        }
        else{
            reply = STANDARD_JSON_ERROR(3, "profile setting failed: Invalid value for vibrate");
            goto error;
        }
    }
    else{
        reply = STANDARD_JSON_ERROR(3, "profile setting failed: Invalid value for ringer");
        goto error;
    }

    g_debug("in %s : received parameters : ringer = %s \t vibrate = %s ", __FUNCTION__, \
                                                    ( ringer == "ON" || ringer == "on" ? "ON" :"OFF"),\
                                                    ( vibrate == "ON" ||vibrate == "on" ? "ON" :"OFF"));

    CHECK(gState.sendUpdatedProfile(lshandle,message));

    error:
    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}

static bool
_getSoundProfile(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    bool subscribe;
    CLSError lserror;
    bool subscribed = false;
    LSMessageJsonParser    msg(message, SCHEMA_1(OPTIONAL(subscribe, boolean)));
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    if (LSMessageIsSubscription (message))
    {
        if (!LSSubscriptionProcess (lshandle, message, &subscribed, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }
    CHECK(gState.respondProfileRequest (lshandle, message, subscribed));

    return true;
}

bool State::respondProfileRequest(LSHandle *sh, LSMessage *message, bool subscribed) {
    pbnjson::JValue    reply = pbnjson::Object();
    reply.put("returnValue", true);
    const std::string name = "SoundProfile";
    bool vibrateWhenRingerOn;
    bool vibrateWhenRingerOff;
    bool ringerOn = gState.getRingerOn();

    gState.getPreference(cPref_VibrateWhenRingerOn, vibrateWhenRingerOn);
    gState.getPreference(cPref_VibrateWhenRingerOff, vibrateWhenRingerOff);

    if (ringerOn && vibrateWhenRingerOn)
        reply.put(name.c_str(), "Sound & Vibrate");
    else if (ringerOn && !vibrateWhenRingerOn)
        reply.put(name.c_str(), "Sound");
    else if (!ringerOn && vibrateWhenRingerOff)
        reply.put(name.c_str(), "Vibrate only");
    else if (!ringerOn && !vibrateWhenRingerOff)
        reply.put(name.c_str(), "Silent");

    reply.put("subscribed", subscribed);
    CLSError lserror;
    bool result = true;

    result = LSMessageReply(sh, message, jsonToString(reply).c_str(), &lserror);
    if (!result)
        lserror.Print(__FUNCTION__, __LINE__);

    return result;
}

bool State::sendUpdatedProfile(LSHandle *sh, LSMessage *message) {
    pbnjson::JValue    reply = pbnjson::Object();
    reply.put("returnValue", true);
    const std::string name = "SoundProfile";
    bool vibrateWhenRingerOn;
    bool vibrateWhenRingerOff;
    bool ringerOn = gState.getRingerOn();

    gState.getPreference(cPref_VibrateWhenRingerOn, vibrateWhenRingerOn);
    gState.getPreference(cPref_VibrateWhenRingerOff, vibrateWhenRingerOff);

    if (ringerOn && vibrateWhenRingerOn)
        reply.put(name.c_str(), "Sound & Vibrate");
    else if (ringerOn && !vibrateWhenRingerOn)
        reply.put(name.c_str(), "Sound");
    else if (!ringerOn && vibrateWhenRingerOff)
        reply.put(name.c_str(), "Vibrate only");
    else if (!ringerOn && !vibrateWhenRingerOff)
        reply.put(name.c_str(), "Silent");

    CLSError lserror;
    bool result = true;
    result = LSSubscriptionPost(sh, "/state", "getSoundProfile", jsonToString(reply).c_str(), &lserror);
    if (!result)
        lserror.Print(__FUNCTION__, __LINE__);

    return result;
}

static bool
_getVolumeBalance(LSHandle *lshandle,
                    LSMessage *message,
                    void *ctx)
{
     std::string reply;
	int balance;
	LSMessageJsonParser msg(message, SCHEMA_0);
	if (!msg.parse(__FUNCTION__, lshandle, eLogOption_LogMessageWithCategory))
		return true;

	balance = gState.getSoundBalance();

	if (balance < cBalance_Min || balance > cBalance_Max) {
		reply = STANDARD_JSON_ERROR(3,
                           "balance not in the range  (invalid scenario name?)");
	} else
	reply = string_printf("{\"returnValue\":true,\"balanceVolume\":%i}",
                             balance);
	CLSError lserror;
	if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
		lserror.Print(__FUNCTION__, __LINE__);

	return true;
}



int  State::getSoundBalance()
 {
    int balance;
    gState.getPreference(cPref_VolumeBalanceOnHeadphones,balance);
    return balance;

 }

bool State::setSoundBalance(int balance){
   return  gState.setPreference(cPref_VolumeBalanceOnHeadphones, balance);
}

bool
_setVolumeBalance(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    const char * schema = SCHEMA_1(REQUIRED(balance, integer));
    LSMessageJsonParser    msg(message, schema);
    const char * reply = STANDARD_JSON_SUCCESS;
    const char* new_balance = "balance";

    int balance;

    if (!msg.parse(__FUNCTION__, lshandle))
    {
       reply = STANDARD_JSON_ERROR(3, "profile setting failed: missing parameters ?");
       goto error;
    }

    if (!msg.get(new_balance, balance))
    {
        reply = MISSING_PARAMETER_ERROR(balance, integer);
        goto error;
    }

    if (balance < cBalance_Min|| balance > cBalance_Max)
    {
        reply = STANDARD_JSON_ERROR(3, "Value is outside the boundary(-10 and +10)");
        goto error;
    }

    if (gState.getSoundBalance() != balance)
    {
        g_debug("Volume balance current = %d and new =%d",gState.getSoundBalance(),balance);
        gState.setSoundBalance(balance);
        gAudioMixer.programBalance(balance);
    }

error:

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}
static bool
_loopback(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    std::string loopback;
    LSMessageJsonParser    msg(message,
                               SCHEMA_1(REQUIRED(loopback, string)));
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    const char * reply = STANDARD_JSON_ERROR(2, "no setting to change found");
    if (!msg.get("loopback", loopback))
        goto error;

    if (loopback == "SPK_ON" || loopback == "SPK_OFF" || loopback == "SUBMIC_ON" || loopback == "SUBMIC_OFF")
    {
        if(gAudioMixer.loopback_set_parameters(loopback.c_str()))
            reply = STANDARD_JSON_SUCCESS;
    } else if (loopback == "BT_ON" || loopback == "BT_OFF") {
        if (gState.getHfpStatus()){
            if(loopback == "BT_ON"){
                getMediaModule()->pauseA2DP();
                gState.btOpenSCO(lshandle);
                gState.setLoopbackStatus(true);
            }
            else {
                getMediaModule()->resumeA2DP();
                gState.btCloseSCO(lshandle);
                gState.setLoopbackStatus(false);
            }

            if(gAudioMixer.loopback_set_parameters(loopback.c_str()))
                reply = STANDARD_JSON_SUCCESS;
        } else
            reply = STANDARD_JSON_ERROR(2, "BT not connected");
    }

error:
    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}

static bool
_setNREC(LSHandle *lshandle, LSMessage *message, void *ctx)
{
     bool NRECOn = TRUE;
     const char * reply = STANDARD_JSON_SUCCESS;

     LSMessageJsonParser    msg(message,
                                        SCHEMA_1(REQUIRED(NRECOn, boolean)));

    if (!msg.parse(__FUNCTION__, lshandle)) {
       reply = STANDARD_JSON_ERROR(3, "NREC setting failed: missing parameters ?");
       goto error;
    }
    if (!msg.get("NRECOn", NRECOn)) {
       reply = STANDARD_JSON_ERROR(3, "NREC setting failed: missing parameters ?");
       goto error;
    } else
       gAudioMixer.setNREC(NRECOn);

error:
    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;

}

#if defined(AUDIOD_PALM_LEGACY)
static bool
_getState(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    LSMessageJsonParser    msg(message, SCHEMA_2(OPTIONAL(name, string),
                                                 OPTIONAL(names, array)));
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    pbnjson::JValue    reply = pbnjson::Object();

    std::vector<std::string> names;

    std::string    name;
    if (msg.get("name", name))
        names.push_back(name);

    pbnjson::JValue        namesArray = msg.get()["names"];
    for (ssize_t index = namesArray.arraySize(); --index >= 0; )
    {
        if (namesArray[index].asString(name) == CONV_OK)
            names.push_back(name);
    }

    for (size_t index = 0; index < names.size(); ++index)
    {
        name = names[index];
        if (name == "adjustMicGain" || name == "all")
            reply.put("adjustMicGain", gState.getAdjustMicGain());

        if (name == "forceMicOn" || name == "all")
            reply.put("forceMicOn", gState.getForceMicOn());

        if (name == "displayOn" || name == "all")
            reply.put("displayOn", gState.getDisplayOn());

        if (name == "sliderState" || name == "all")
            reply.put("sliderState", (bool)(gState.getSliderState() == eSlider_Open));

        if (name == "headsetState" || name == "all")
            reply.put("headsetState", _getHeadsetStateName(gState.getHeadsetState()));
    }

    if (reply.begin() != reply.end())
        reply.put("returnValue", true);
    else
        reply = createJsonReply(false, 2, "no setting found");

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, jsonToString(reply).c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}
#endif

bool
State::setPreferenceRequest(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    LSMessageJsonParser    msg(message, SCHEMA_ANY);
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    const char * reply = STANDARD_JSON_ERROR(2, "found no setting to change");
    const char * success = STANDARD_JSON_SUCCESS;

    pbnjson::JValue request = msg.get();
    for (pbnjson::JValue::ObjectIterator pair = request.begin();
                                              pair != request.end(); pair++)
    {
        std::string    name;

        if (VERIFY((*pair).first.asString(name) == CONV_OK) &&
                                         !IS_SYSTEM_PARAMETER(name.c_str()))
        {
            bool found = false;
            bool boolValue;
            std::string    stringValue;
            g_debug("name.c_str = %s", name.c_str());
            if ((*pair).second.asBool(boolValue) == CONV_OK)
            {
                TBooleanPreferences::iterator iter = gState.mBooleanPreferences.find(name);
                if (iter != gState.mBooleanPreferences.end())
                {
                    iter->second.mValue = boolValue;
                    g_debug("Boolean");
                    found = true;

                    if (msg.get("BeatsOnForHeadphones", boolValue)){
                        gAudioDevice.setBeatsOnForHeadphones(boolValue);
                    }

                }
            }
            else if ((*pair).second.asString(stringValue) == CONV_OK)
            {
                TStringPreferences::iterator iter = gState.mStringPreferences.find(name);
                if (iter != gState.mStringPreferences.end())
                {
                    iter->second.mValue = stringValue;
                    g_debug("String");
                    found = true;
                }
            }
            if (found)
                reply = success;
            else
                g_warning("State::setPreference: invalid preference '%s'",
                                   name.c_str());    // name or type incorrect
        }
    }

    // if we changed any setting, we save, reply with success and only log warnings
    if (reply == success)
        gState.storePreferences();

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, reply, &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    g_debug("- %s", __FUNCTION__);
    return true;
}

bool
State::getPreferenceRequest(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    LSMessageJsonParser    msg(message, SCHEMA_2(OPTIONAL(name, string), OPTIONAL(names, array)));
    if (!msg.parse(__FUNCTION__, lshandle))
        return true;
    pbnjson::JValue    reply = pbnjson::Object();

    std::vector<std::string> names;

    std::string    name;
    if (msg.get("name", name))
        names.push_back(name);
    pbnjson::JValue        namesArray = msg.get()["names"];
    for (ssize_t index = namesArray.arraySize(); --index >= 0; )
    {
        if (namesArray[index].asString(name) == CONV_OK)
            names.push_back(name);
    }

    TBooleanPreferences::iterator    boolPref;
    TStringPreferences::iterator    stringPref;
    for (size_t index = 0; index < names.size(); ++index)
    {
        const std::string & name = names[index];
        if ((boolPref = gState.mBooleanPreferences.find(name)) !=
                                             gState.mBooleanPreferences.end()){
            reply.put(name.c_str(), boolPref->second.mValue);
        }
        if ((stringPref = gState.mStringPreferences.find(name)) !=
                                              gState.mStringPreferences.end()){
            reply.put(name.c_str(), stringPref->second.mValue);
        }
    }

    if (reply.begin() != reply.end())
        reply.put("returnValue", true);
    else
        reply = createJsonReply(false, 2, "no setting found");

    CLSError lserror;
    if (!LSMessageReply(lshandle, message, jsonToString(reply).c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    g_debug("- %s", __FUNCTION__);
    return true;
}

void State::rtpSubscriptionReply(int info, char *ip, int port)
{
    std::string key = "/state/loadRTPModule";
    pbnjson::JValue replyString = pbnjson::Object();
    CLSError lserror;

    if (info)
    {
        replyString = createJsonReply(false, UNSUPPORTED_ERROR_CODE, "Unable to load RTP module");
        gState.setRTPLoaded(false);
    }
    else
    {
        replyString.put("returnValue", true);
        replyString.put("ip", ip);
        replyString.put("port", port);
    }
    if(!LSSubscriptionReply(GetPalmService(), key.c_str(), jsonToString(replyString).c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
}

    /*
     * Load RTP module in pulseaudio
     * Provide PCM stream to Pulseaudio for Multicast
     */
static bool _loadRTPModule(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    //const char *reply = STANDARD_JSON_SUCCESS;
    pbnjson::JValue reply = pbnjson::Object();
    std::string type;
    bool subscribed = false;
    std::string ip;
    int port;
    CLSError lserror;

    LSMessageJsonParser msg(message, SCHEMA_4(REQUIRED(type, string), REQUIRED(subscribe, boolean), OPTIONAL(ip, string), OPTIONAL(port, integer)));

    if (!msg.parse(__FUNCTION__, lshandle))
        return true;

    /* check RTP loaded or not */
    if (gState.isRTPLoaded()) {
        reply = createJsonReply(false, REPEATED_REQUEST_ERROR_CODE, "RTP is already loaded");
        goto error;
    }

    if(!msg.get("type", type)) {
        reply = createJsonReply(false, UNSUPPORTED_ERROR_CODE, "required prop not found: 'type'");
        goto error;
    }

    if(!strcmp(type.c_str(), "unicast")) {
        if (!msg.get("ip", ip)) {
            reply = createJsonReply(false, UNSUPPORTED_ERROR_CODE, "IP is required for unicast");
            goto error;
        }
    } else if(!strcmp(type.c_str(), "multicast")) {
        if (!msg.get("ip", ip))
            ip = "default";
    } else {
        reply = createJsonReply(false, UNSUPPORTED_ERROR_CODE, "Unsupported type");
        goto error;
    }

    if(!msg.get("port", port))
        port = 0;

    /* Subscribe */
    if (LSMessageIsSubscription (message)) {
        if (!LSSubscriptionProcess (lshandle, message, &subscribed, &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }

    if(!gAudioMixer.programLoadRTP(type.c_str(), ip.c_str(), port)) {
        reply = createJsonReply(false, UNSUPPORTED_ERROR_CODE, "Failed to send message to pulseaudio");
        goto error;
    }
    gState.setRTPLoaded(true);

    gAudioMixer.programDestination (emedia, eRtpsink);


    reply.put("returnValue", true);
    reply.put("subscribe", subscribed);

error:
    if (!LSMessageReply(lshandle, message, jsonToString(reply).c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}

static bool _unloadRTPModule(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    g_message("[%s:%d] [B4 stop Upload]", __FUNCTION__, __LINE__);

    callbackReceived = true;
    pbnjson::JValue reply = pbnjson::Object();
    CLSError lserror;

    LSMessageJsonParser msg(message, SCHEMA_0);

    if (!gState.isRTPLoaded()) {
        reply = createJsonReply(false, REPEATED_REQUEST_ERROR_CODE, "RTP module is not loaded");
        goto error;
    } 

    if(!gAudioMixer.programUnloadRTP()) {
        reply = createJsonReply(false, UNSUPPORTED_ERROR_CODE, "Failed to send message to pulseaudio");
        goto error;
    }


    gState.setRTPLoaded(false);
    if (ScenarioModule * module = dynamic_cast <ScenarioModule *> (ScenarioModule::getCurrent()))
            module->programSoftwareMixer(true);

    reply.put("returnValue", true);

error:
    if (!LSMessageReply(lshandle, message, jsonToString(reply).c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);

    return true;
}

void State::setA2DPStatus(bool Status)
{
    mA2DPStatus = Status;
}

bool State::getA2DPStatus()
{
    return mA2DPStatus;
}

std::string State::getHFPConnectionStatus()
{
    return mHfpStatus;
}

void State::setHFPConnectionStatus(std::string Status)
{
    mHfpStatus = Status;
}

void State::setQvoiceStartedStatus(bool status)
{
    mQvoiceStarted = status;
}

bool State::isQvoiceStarted()
{
    return mQvoiceStarted;
}

GlobalConf::GlobalConf()
:m_carrierbusyToneRepeats(DEFAULT_CARRIER_BUSYTONE_REPEATS),
m_carrierEmergencyToneRepeats(DEFAULT_CARRIER_EMERGENCYTONE_REPEATS)
{
    GKeyFile *keyfile = g_key_file_new();
    GError* err=NULL;
    if (!g_key_file_load_from_file(keyfile,
        "/etc/audio/a/global.conf",
         G_KEY_FILE_NONE, NULL)) {
        if (!g_key_file_load_from_file(keyfile,
           "/etc/audio/b/global.conf",
            G_KEY_FILE_NONE, NULL)) {
            if (!g_key_file_load_from_file(keyfile,
               "/etc/audio/c/global.conf",
                G_KEY_FILE_NONE, NULL)) {
                g_debug("no global.conf");
                goto cleanup;
            }
        }
    }
    g_debug("global.conf");
    m_carrierbusyToneRepeats = g_key_file_get_integer(keyfile,
                                                      "carrier",
                                                      "busy_tone",
                                                       NULL);
    if (err != NULL) m_carrierbusyToneRepeats = DEFAULT_CARRIER_BUSYTONE_REPEATS;
    else g_debug("  busy_tone -> %d", m_carrierbusyToneRepeats);

    m_carrierEmergencyToneRepeats = g_key_file_get_integer(keyfile,
                                                           "carrier",
                                                           "emergency_tone",
                                                            NULL);
    if (err != NULL) m_carrierEmergencyToneRepeats = DEFAULT_CARRIER_EMERGENCYTONE_REPEATS;
    else g_debug("  emergency_tone -> %d", m_carrierEmergencyToneRepeats);

cleanup:
    g_key_file_free(keyfile);
}

static LSMethod stateMethods[] = {
#if defined(AUDIOD_PALM_LEGACY)
    { "set", _setState},
    { "get", _getState},
#endif
    { "getPreference", State::getPreferenceRequest},
    { "getVolumeBalance", _getVolumeBalance},
    { "setRingerSwitch", _setRingerSwitch},
    { "getSoundProfile", _getSoundProfile},
    { "setSoundProfile", _setSoundProfile},
    { "getTouchSound", _getTouchSound},
    { },
};

int
ServiceInterfaceInit(GMainLoop *loop, LSHandle *handle)
{
    CLSError lserror;
    LSHandle *sh;
    bool result;
    result = ServiceRegisterCategory ("/state", stateMethods, NULL, NULL);
    if (!result)
    {
        lserror.Print(__FUNCTION__, __LINE__);
        g_message("%s: Registering Service for '%s' category failed", __FUNCTION__, "/state");
        return (-1);
    }

    sh = GetPalmService();
    if (!sh) {
        g_critical("ServiceInterfaceInit() : no LSHandle acquired. aborting\n");
        return -1;
    }

    return 0;
}

INIT_FUNC (StateInit);
CONTROL_START_FUNC (ControlInterfaceInit);
SERVICE_START_FUNC (ServiceInterfaceInit);
