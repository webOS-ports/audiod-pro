// Copyright (c) 2018-2020 LG Electronics, Inc.
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

#include "volumeSettings.h"

bool volumeSettings::_setVolume(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    g_debug("MasterVolume: setVolume");
    LSMessageJsonParser msg(message, STRICT_SCHEMA(PROPS_3(PROP(soundOutput, string), PROP(volume, integer), PROP(sessionId, integer)) REQUIRED_2(soundOutput, volume)));
    if (!msg.parse(__FUNCTION__,lshandle))
        return true;

    bool status = false;
    std::string soundOutput;
    int display = DISPLAY_ONE;
    bool isValidVolume = false;
    int displayId = DISPLAY_ONE;
    int volume = MIN_VOLUME;
    std::string reply = STANDARD_JSON_SUCCESS;

    msg.get("soundOutput", soundOutput);
    msg.get("volume", volume);
    msg.get("sessionId", display);

    if ((volume >= MIN_VOLUME) && (volume <= MAX_VOLUME))
        isValidVolume = true;

    if (DISPLAY_TWO == display)
        displayId = DEFAULT_TWO_DISPLAY_ID;
    else
        displayId = DEFAULT_ONE_DISPLAY_ID;

    g_debug("SetMasterVolume with soundout: %s volume: %d display: %d", soundOutput.c_str(), volume, displayId);
    volumeSettings* volumeInstance = volumeSettings::getVolumeSettingsInstance();
    AudioMixer* audioMixerObj = AudioMixer::getAudioMixerInstance();
    if (DISPLAY_TWO == display)
    {
        if (soundOutput != "alsa")
        {
            g_debug("Not a valid soundOutput");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_SOUNDOUT, "Volume control is not supported");
        }
        else if ((volumeInstance) && (isValidVolume) && (audioMixerObj) && (audioMixerObj->setVolume(displayId, volume)))
        {
            g_debug("set volume %d for display: %d", volume, displayId);
            volumeInstance->displayTwoVolume = volume;
            std::string callerId = LSMessageGetSenderServiceName(message);
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
            pbnjson::JValue setVolumeResponse = pbnjson::Object();
            setVolumeResponse.put("returnValue", true);
            setVolumeResponse.put("volume", volume);
            setVolumeResponse.put("soundOutput", soundOutput);
            reply = setVolumeResponse.stringify();
        }
        else
        {
            g_debug("Did not able to set volume %d for display: %d", volume, displayId);
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_NOT_SUPPORT_VOLUME_CHANGE, "SoundOutput volume is not in range");
        }

        CLSError lserror;
        if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }
    else
    {
        if ((volumeInstance) && (isValidVolume) && (audioMixerObj) && (audioMixerObj->setVolume(displayId, volume)))
        {
            g_debug("set volume %d for display: %d", volume, displayId);
            volumeInstance->displayOneVolume = volume;
            std::string callerId = LSMessageGetSenderServiceName(message);
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
            status = true;
        }
        else
        {
            g_debug("Did not able to set volume %d for display: %d", volume, displayId);
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_NOT_SUPPORT_VOLUME_CHANGE, "SoundOutput volume is not in range");
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
                lserror.Print(__FUNCTION__, __LINE__);
        }
        envelopeRef *envelope = new (std::nothrow)envelopeRef;
        if (nullptr != envelope)
        {
            envelope->message = message;
            envelope->context = (volumeSettings*)ctx;
            volumeSettings *volumeSettingsObj = (volumeSettings*)ctx;

            if ((nullptr != volumeSettingsObj->mixerObj) && (isValidVolume))
            {
                if(volumeSettingsObj->mixerObj->setMasterVolume(soundOutput, volume, _setVolumeCallBack, envelope))
                {
                    g_debug("MasterVolume: SetMasterVolume umimixer call successfull");
                    LSMessageRef(message);
                    status = true;
                }
                else
                {
                    g_debug("MasterVolume: SetMasterVolume umimixer call failed");
                    reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_FAILED_MIXER_CALL, "Internal error");
                }
            }
            else
            {
                g_debug("MasterVolume: gumiaudiomixer is NULL");
                reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_MIXER_INSTANCE, "Internal error");
            }
        }
        else
        {
            g_debug("MasterVolume: SetMasterVolume envelope is NULL");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_ENVELOPE_INSTANCE , "Internal error");
        }
        if (false == status)
        {
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            if (nullptr != envelope)
            {
                delete envelope;
                envelope = nullptr;
            }
        }
    }
    return true;
}

bool volumeSettings::_setVolumeCallBack(LSHandle *sh, LSMessage *reply, void *ctx)
{
    g_debug("MasterVolume: setVolumeCallBack");
    std::string payload = LSMessageGetPayload(reply);
    JsonMessageParser ret(payload.c_str(), NORMAL_SCHEMA(PROPS_1(PROP(returnValue, boolean)) REQUIRED_1(returnValue)));
    bool returnValue = false;
    if (ret.parse(__FUNCTION__))
    {
        ret.get("returnValue", returnValue);
    }
    std::string soundOutput;
    int iVolume = 0;
    if (returnValue)
    {
        JsonMessageParser data(payload.c_str(), NORMAL_SCHEMA(PROPS_2(PROP(volume, integer),\
            PROP(soundOutput, string)) REQUIRED_2(soundOutput, volume)));
        if (data.parse(__FUNCTION__))
        {
            data.get("soundOutput", soundOutput);
            data.get("volume", iVolume);
            g_debug("MasterVolume::Successfully Set the speaker volume for sound out %s with volume %d", soundOutput.c_str(), iVolume);
        }
        else
        {
            returnValue = false;
        }
    }
    else
    {
        g_debug("MasterVolume: Could not SetMasterVolume");
    }
    if (nullptr != ctx)
    {
        envelopeRef *envelope = (envelopeRef*)ctx;
        LSMessage *message = (LSMessage*)envelope->message;
        volumeSettings *volumeSettingsObj = (volumeSettings*)envelope->context;
        if (true == returnValue)
        {
            if (nullptr != volumeSettingsObj)
            {
                volumeSettingsObj->setCurrentVolume(iVolume);
            }
        }
        if (nullptr != message)
        {
            CLSError lserror;
            if (!LSMessageRespond(message, payload.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            LSMessageUnref(message);
        }
        else
        {
            g_debug("MasterVolume: internal mixer call");
        }
        if (nullptr != envelope)
        {
            delete envelope;
            envelope = nullptr;
        }
    }
    else
    {
        g_debug("MasterVolume: context is null");
    }
    return true;
}

std::string volumeSettings::getVolumeInfo(const int &displayId, const std::string &callerId)
{
    pbnjson::JValue soundOutInfo = pbnjson::Object();
    pbnjson::JValue volumeStatus = pbnjson::Object();
    int volume = MIN_VOLUME;
    bool muteStatus = false;
    int display = DISPLAY_ONE;
    if (DEFAULT_ONE_DISPLAY_ID == displayId)
    {
        volume = displayOneVolume;
        muteStatus = displayOneMuteStatus;
        display = DISPLAY_ONE;
    }
    else
    {
        volume = displayTwoVolume;
        muteStatus = displayTwoMuteStatus;
        display = DISPLAY_TWO;
    }

    volumeStatus = {{"muted", muteStatus},
                    {"volume", volume},
                    {"soundOutput", "alsa"},
                    {"sessionId", display}};

    soundOutInfo.put("volumeStatus", volumeStatus);
    soundOutInfo.put("returnValue", true);
    soundOutInfo.put("callerId", callerId);

    return soundOutInfo.stringify();
}

void volumeSettings::notifyVolumeSubscriber(const int &displayId, const std::string &callerId)
{
    CLSError lserror;
    std::string reply = getVolumeInfo(displayId, callerId);
    g_debug("[%s] reply message to subscriber: %s", __FUNCTION__, reply.c_str());
    if (!LSSubscriptionReply(GetPalmService(), AUDIOD_API_GET_VOLUME, reply.c_str(), &lserror))
    {
        lserror.Print(__FUNCTION__, __LINE__);
        g_debug("Notify error");
    }
}

void volumeSettings::setMuteStatus(const int &displayId)
{
    AudioMixer* audioMixerObj = AudioMixer::getAudioMixerInstance();
    if (DEFAULT_ONE_DISPLAY_ID == displayId && audioMixerObj)
    {
        audioMixerObj->setMute(displayId, displayOneMuteStatus);
        g_debug("set mute status %d for display: %d", displayOneMuteStatus, displayId);
    }
    else if (DEFAULT_TWO_DISPLAY_ID == displayId && audioMixerObj)
    {
        audioMixerObj->setMute(displayId, displayTwoMuteStatus);
        g_debug("set mute status %d for display: %d", displayTwoMuteStatus, displayId);
    }
    else
    {
        if (audioMixerObj)
        {
            audioMixerObj->setMute(displayId, displayOneMuteStatus);
            audioMixerObj->setMute(displayId, displayTwoMuteStatus);
            g_debug("set mute status is %d for display one and mute status is %d for display two", displayOneMuteStatus, displayTwoMuteStatus);
        }
    }
}

void volumeSettings::setVolume(const int &displayId)
{
    AudioMixer* audioMixerObj = AudioMixer::getAudioMixerInstance();
    int volume = 0;
    if (DEFAULT_ONE_DISPLAY_ID == displayId)
        volume = displayOneVolume;
    else if (DEFAULT_TWO_DISPLAY_ID == displayId)
        volume = displayTwoVolume;
    if (audioMixerObj && audioMixerObj->setVolume(displayId, volume))
        g_debug("set volume %d for display: %d", volume, displayId);
    else
        g_debug("Did not able to set volume %d for display: %d", volume, displayId);
}

bool volumeSettings::_getVolume(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    g_debug("MasterVolume: getVolume");
    LSMessageJsonParser msg(message, STRICT_SCHEMA(PROPS_2(PROP(subscribe, boolean), PROP(sessionId, integer))));
    if (!msg.parse(__FUNCTION__,lshandle))
        return true;

    int display = DISPLAY_ONE;
    bool subscribed = false;
    CLSError lserror;
    std::string reply = STANDARD_JSON_SUCCESS;

    msg.get("subscribe", subscribed);
    msg.get("sessionId", display);

    if (LSMessageIsSubscription (message))
    {
        if (!LSSubscriptionProcess(lshandle, message, &subscribed, &lserror))
        {
            lserror.Print(__FUNCTION__, __LINE__);
            g_debug("LSSubscriptionProcess failed");
            return true;
        }
    }

    envelopeRef *envelope = new (std::nothrow)envelopeRef;
    volumeSettings* volumeInstance = volumeSettings::getVolumeSettingsInstance();
    if (nullptr != envelope)
    {
        envelope->message = message;
        envelope->context = (volumeSettings*)ctx;
        volumeSettings *volumeSettingsObj = (volumeSettings*)ctx;
        if (nullptr != volumeSettingsObj)
        {
            int displayId = DEFAULT_ONE_DISPLAY_ID;
            std::string callerId = LSMessageGetSenderServiceName(message);
            if (DISPLAY_TWO == display)
                displayId = DEFAULT_TWO_DISPLAY_ID;

            reply = volumeInstance->getVolumeInfo(displayId, callerId);
        }
        else
        {
            g_debug("MasterVolume: volumeSettingsObj is NULL");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_MIXER_INSTANCE, "Internal error");
        }
    }
    else
    {
        g_debug("MasterVolume: getVolume envelope is NULL");
        reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_ENVELOPE_INSTANCE , "Internal error");
    }

    g_debug("%s : Reply:%s", reply.c_str(), __FUNCTION__);
    if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
        lserror.Print(__FUNCTION__, __LINE__);
    return true;
}
bool volumeSettings::_getVolumeCallBack(LSHandle *sh, LSMessage *reply, void *ctx)
{
    g_debug("MasterVolume: getVolumeCallBack");
    std::string payload = LSMessageGetPayload(reply);
    JsonMessageParser ret(payload.c_str(), NORMAL_SCHEMA(PROPS_1(PROP(returnValue, boolean)) REQUIRED_1(returnValue)));
    bool returnValue = false;
    if (ret.parse(__FUNCTION__))
    {
        ret.get("returnValue", returnValue);
    }
    if (returnValue)
    {
        JsonMessageParser data(payload.c_str(), NORMAL_SCHEMA(PROPS_1(PROP(volumeStatus, array)) REQUIRED_1(volumeStatus)));
        if (data.parse(__FUNCTION__))
        {
            pbnjson::JSchemaFragment inputSchema("{}");
            pbnjson::JDomParser parser(NULL);
            parser.parse(payload, inputSchema, NULL);
            pbnjson::JValue parsed = parser.getDom();
            pbnjson::JValue volumeStatus = parsed["volumeStatus"];
            if (!parsed["volumeStatus"].isArray())
            {
                g_debug("MasterVolume: invalid volumeStatus array");
            }
            else
            {
                returnValue = true;
                for (int i = 0; i < volumeStatus.arraySize(); ++i)
                {
                    bool bMuteSstatus = volumeStatus[i]["muted"].asBool();
                    std::string strSoundOutput = volumeStatus[i]["SoundOutput"].asString();
                    int iVolume = volumeStatus[i]["volume"].asNumber<int>();
                    g_debug("MasterVolume: MuteStatus: %d Soundout: %s volume: %d \n", (int)bMuteSstatus, strSoundOutput.c_str(), iVolume);
                }
            }
        }
        else
        {
            returnValue = false;
        }
    }
    else
    {
        g_debug("MasterVolume: Could not GetMasterVolume");
    }
    if (nullptr != ctx)
    {
         envelopeRef *envelope = (envelopeRef*)ctx;
         LSMessage *message = (LSMessage*)envelope->message;
         volumeSettings *volumeSettingsObj = (volumeSettings*)envelope->context;
         if (nullptr != message)
         {
             CLSError lserror;
             if (!LSMessageRespond(message, payload.c_str(), &lserror))
             {
                 lserror.Print(__FUNCTION__, __LINE__);
             }
             LSMessageUnref(message);
         }
         else
         {
             g_debug("MasterVolume: internal mixer call");
         }
         if (nullptr != envelope)
         {
             delete envelope;
             envelope = nullptr;
         }
    }
    else
    {
        g_debug("MasterVolume: context is null");
    }
    return true;
}

bool volumeSettings::_muteVolume(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    g_debug("MasterVolume: muteVolume");
    LSMessageJsonParser msg(message, STRICT_SCHEMA(PROPS_3(PROP(soundOutput, string), PROP(mute, boolean), PROP(sessionId, integer)) REQUIRED_2(soundOutput, mute)));
    if (!msg.parse(__FUNCTION__,lshandle))
        return true;
    std::string soundOutput;
    bool mute = false;
    bool status = false;
    int displayId = DISPLAY_ONE;
    int display;
    std::string reply = STANDARD_JSON_SUCCESS;

    msg.get("soundOutput", soundOutput);
    msg.get("mute", mute);
    msg.get("sessionId", display);

    if (DISPLAY_ONE == display)
        displayId = 1;
    else if (DISPLAY_TWO == display)
        displayId = 2;
    else
        displayId = 3;

    g_debug("muteVolume with soundout: %s mute status: %d",soundOutput.c_str(),(int)mute);
    envelopeRef *envelope = new (std::nothrow)envelopeRef;
    volumeSettings* volumeInstance = volumeSettings::getVolumeSettingsInstance();
    AudioMixer* audioMixerObj = AudioMixer::getAudioMixerInstance();
    std::string callerId = LSMessageGetSenderServiceName(message);
    if (DISPLAY_TWO == display)
    {
        if (audioMixerObj && audioMixerObj->setMute(displayId, mute))
        {
            volumeInstance->displayTwoMuteStatus = mute;
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
            pbnjson::JValue muteVolumeResponse = pbnjson::Object();
            muteVolumeResponse.put("returnValue", true);
            muteVolumeResponse.put("muteStatus", mute);
            muteVolumeResponse.put("soundOutput", soundOutput);
            reply = muteVolumeResponse.stringify();
        }
        else
        {
            g_debug("Did not able to mute volume %d for display: %d", mute, displayId);
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_MIXER_INSTANCE, "Internal error");
        }

        CLSError lserror;
        if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }
    else
    {
        if (audioMixerObj && audioMixerObj->setMute(displayId, mute))
        {
            g_debug("Successfully set mute volume %d for display: %d", mute, displayId);
            if (DEFAULT_ONE_DISPLAY_ID == displayId)
                volumeInstance->displayOneMuteStatus = mute;
            else
            {
                volumeInstance->displayOneMuteStatus = mute;
                volumeInstance->displayTwoMuteStatus = mute;
            }
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
        }
        else
        {
            g_debug("Did not able to mute volume %d for display: %d", mute, displayId);
        }
        if(nullptr != envelope)
        {
            envelope->message = message;
            envelope->context = (volumeSettings*)ctx;
            volumeSettings *volumeSettingsObj = (volumeSettings*)ctx;
            if (nullptr != volumeSettingsObj->mixerObj)
            {
                if(volumeSettingsObj->mixerObj->masterVolumeMute(soundOutput, mute, _muteVolumeCallBack, envelope))
                {
                    g_debug("MasterVolume: masterVolumeMute umimixer call successfull");
                    LSMessageRef(message);
                    status = true;
                }
                else
                {
                    g_debug("MasterVolume: masterVolumeMute umimixer call failed");
                    reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_FAILED_MIXER_CALL, "Internal error");
                }
            }
            else
            {
                g_debug("MasterVolume: gumiaudiomixer is NULL");
                reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_MIXER_INSTANCE, "Internal error");
            }
        }
        else
        {
            g_debug("MasterVolume: muteVolume envelope is NULL");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_ENVELOPE_INSTANCE , "Internal error");
        }
        if (false == status)
        {
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            if (nullptr != envelope)
            {
                delete envelope;
                envelope = nullptr;
            }
        }
    }
    return true;
}

bool volumeSettings::_muteVolumeCallBack(LSHandle *sh, LSMessage *reply, void *ctx)
{
    g_debug("MasterVolume: muteVolumeCallBack");
    std::string payload = LSMessageGetPayload(reply);
    JsonMessageParser ret(payload.c_str(), NORMAL_SCHEMA(PROPS_1(PROP(returnValue, boolean)) REQUIRED_1(returnValue)));
    bool returnValue = false;
    if (ret.parse(__FUNCTION__))
    {
        ret.get("returnValue", returnValue);
    }
    std::string soundOutput;
    bool bMute = false;
    if (returnValue)
    {
        JsonMessageParser data(payload.c_str(), NORMAL_SCHEMA(PROPS_2(PROP(mute, boolean),\
            PROP(soundOutput, string)) REQUIRED_2(soundOutput, mute)));
        if (data.parse(__FUNCTION__))
        {
           data.get("mute", bMute);
           data.get("soundOutput", soundOutput);
           g_debug("MasterVolume:Successfully muted/unmuted the soundout %s and mute status %d ", soundOutput.c_str(), bMute);
        }
        else
        {
            returnValue = false;
        }
    }
    else
    {
        g_debug("MasterVolume: Could not mute MasterVolume");
    }
    if (nullptr != ctx)
    {
        envelopeRef *envelope = (envelopeRef*)ctx;
        LSMessage *message = (LSMessage*)envelope->message;
        volumeSettings *volumeSettingsObj = (volumeSettings*)envelope->context;
        if(true == returnValue)
        {
            if (nullptr != volumeSettingsObj)
            {
                volumeSettingsObj->setCurrentMuteStatus(bMute);
            }
        }
        if (nullptr != message)
        {
            CLSError lserror;
            if (!LSMessageRespond(message, payload.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            LSMessageUnref(message);
        }
        else
        {
            g_debug("MasterVolume: internal mixer call");
        }
        if (nullptr != envelope)
        {
            delete envelope;
            envelope = nullptr;
        }
    }
    else
    {
        g_debug("MasterVolume: context is null");
    }
    return true;
}

bool volumeSettings::_volumeUp(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    g_debug("MasterVolume: volumeUp");
    LSMessageJsonParser msg(message, STRICT_SCHEMA(PROPS_2(PROP(soundOutput, string), PROP(sessionId, integer)) REQUIRED_1(soundOutput)));
    if (!msg.parse(__FUNCTION__,lshandle))
        return true;
    std::string soundOutput;
    bool status = false;
    int display = DISPLAY_ONE;
    bool isValidVolume = false;
    int volume = MIN_VOLUME;
    int displayId = DISPLAY_ONE;
    std::string reply = STANDARD_JSON_SUCCESS;

    msg.get("soundOutput", soundOutput);
    msg.get("sessionId", display);

    if (DISPLAY_TWO == display)
        displayId = 2;
    else
        displayId = 1;

    g_debug("MasterVolume: volumeUp with soundout: %s", soundOutput.c_str());
    volumeSettings* volumeInstance = volumeSettings::getVolumeSettingsInstance();
    AudioMixer* audioMixerObj = AudioMixer::getAudioMixerInstance();
    std::string callerId = LSMessageGetSenderServiceName(message);
    if (DISPLAY_TWO == display)
    {
        if ((volumeInstance->displayTwoVolume+1) <= MAX_VOLUME)
        {
            isValidVolume = true;
            volume = volumeInstance->displayTwoVolume+1;
        }
        else
            g_debug("Volume up value not in range");
        if ((isValidVolume) && (audioMixerObj && audioMixerObj->setVolume(displayId, volume)))
        {
            g_debug("set volume %d for display: %d", volume, displayId);
            ++(volumeInstance->displayTwoVolume);
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
            pbnjson::JValue setVolumeResponse = pbnjson::Object();
            setVolumeResponse.put("returnValue", true);
            setVolumeResponse.put("volume", volume);
            setVolumeResponse.put("soundOutput", soundOutput);
            reply = setVolumeResponse.stringify();
        }
        else
        {
            g_debug("Did not able to set volume %d for display: %d", volume, displayId);
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_NOT_SUPPORT_VOLUME_CHANGE, "SoundOutput volume is not in range");
        }
        CLSError lserror;
        if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }
    else
    {
        if ((volumeInstance->displayOneVolume+1) <= MAX_VOLUME)
        {
            isValidVolume = true;
            volume = volumeInstance->displayOneVolume+1;
        }
        else
        {
            g_debug("Volume up value not in range");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_NOT_SUPPORT_VOLUME_CHANGE, "SoundOutput volume is not in range");
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
                lserror.Print(__FUNCTION__, __LINE__);
        }
        if ((isValidVolume) && (audioMixerObj && audioMixerObj->setVolume(displayId, volume)))
        {
            g_debug("set volume %d for display: %d", volume, displayId);
            ++(volumeInstance->displayOneVolume);
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
        }
        envelopeRef *envelope = new (std::nothrow)envelopeRef;
        if(nullptr != envelope)
        {
            envelope->message = message;
            envelope->context = (volumeSettings*)ctx;
            volumeSettings *volumeSettingsObj = (volumeSettings*)ctx;
            if ((nullptr != volumeSettingsObj->mixerObj) && (isValidVolume))
            {
                if(volumeSettingsObj->mixerObj->masterVolumeUp(soundOutput, _volumeUpCallBack, envelope))
                {
                    g_debug("MasterVolume: masterVolumeUp umimixer call successfull");
                    LSMessageRef(message);
                    status = true;
                }
                else
                {
                    g_debug("MasterVolume: masterVolumeUp umimixer call failed");
                    reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_MIXER_INSTANCE, "Internal error");
                }
            }
            else
            {
                g_debug("MasterVolume: gumiaudiomixer is NULL");
                reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_PARAMETER_BE_EMPTY, "Internal error");
            }
        }
        else
        {
            g_debug("MasterVolume: masterVolumeUp envelope is NULL");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_ENVELOPE_INSTANCE , "Internal error");
        }
        if (false == status)
        {
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            if (nullptr != envelope)
            {
                delete envelope;
                envelope = nullptr;
            }
        }
    }
    return true;
}

bool volumeSettings::_volumeUpCallBack(LSHandle *sh, LSMessage *reply, void *ctx)
{
    g_debug("MasterVolume: volumeUpCallBack");
    std::string payload = LSMessageGetPayload(reply);
    JsonMessageParser ret(payload.c_str(), NORMAL_SCHEMA(PROPS_1(PROP(returnValue, boolean)) REQUIRED_1(returnValue)));
    bool returnValue = false;
    if (ret.parse(__FUNCTION__))
    {
        ret.get("returnValue", returnValue);
    }
    std::string soundOutput;
    int iVolume = 0;
    if (returnValue)
    {
        JsonMessageParser data(payload.c_str(), NORMAL_SCHEMA(PROPS_2(PROP(volume, integer),\
            PROP(soundOutput, string)) REQUIRED_2(soundOutput, volume)));
        if (data.parse(__FUNCTION__))
        {
            data.get("soundOutput", soundOutput);
            data.get("volume", iVolume);
            g_debug("MasterVolume::Successfully increased the speaker volume for sound out %s with volume %d", soundOutput.c_str(), iVolume);
        }
        else
        {
            returnValue = false;
        }
    }
    else
    {
        g_debug("MasterVolume: Could not volume up");
    }
    if (nullptr != ctx)
    {
        envelopeRef *envelope = (envelopeRef*)ctx;
        LSMessage *message = (LSMessage*)envelope->message;
        volumeSettings *volumeSettingsObj = (volumeSettings*)envelope->context;
        if(true == returnValue)
        {
            if (nullptr != volumeSettingsObj)
            {
                volumeSettingsObj->setCurrentVolume(iVolume);
            }
        }
        if (nullptr != message)
        {
            CLSError lserror;
            if (!LSMessageRespond(message, payload.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            LSMessageUnref(message);
        }
        else
        {
            g_debug("MasterVolume: internal mixer call");
        }
        if (nullptr != envelope)
        {
            delete envelope;
            envelope = nullptr;
        }
    }
    else
    {
        g_debug("MasterVolume: context is null");
    }
    return true;
}

bool volumeSettings::_volumeDown(LSHandle *lshandle, LSMessage *message, void *ctx)
{
    g_debug("MasterVolume: volumeDown");
    LSMessageJsonParser msg(message, STRICT_SCHEMA(PROPS_2(PROP(soundOutput, string), PROP(sessionId, integer)) REQUIRED_1(soundOutput)));
    if (!msg.parse(__FUNCTION__,lshandle))
        return true;
    std::string soundOutput;
    bool status = false;
    int displayId = DISPLAY_ONE;
    bool isValidVolume = false;
    int volume = MIN_VOLUME;
    int display = DISPLAY_ONE;
    std::string reply = STANDARD_JSON_SUCCESS;

    msg.get("soundOutput", soundOutput);
    msg.get("sessionId", display);

    if (DISPLAY_TWO == display)
        displayId = 2;
    else
        displayId = 1;

    g_debug("MasterVolume: volumeDown with soundout: %s", soundOutput.c_str());
    volumeSettings* volumeInstance = volumeSettings::getVolumeSettingsInstance();
    AudioMixer* audioMixerObj = AudioMixer::getAudioMixerInstance();
    std::string callerId = LSMessageGetSenderServiceName(message);
    if (DISPLAY_TWO == display)
    {
        if ((volumeInstance->displayTwoVolume-1) >= MIN_VOLUME)
        {
            isValidVolume = true;
            volume = volumeInstance->displayTwoVolume-1;
        }
        else
            g_debug("Volume down value not in range");
        if ((isValidVolume) && (audioMixerObj && audioMixerObj->setVolume(displayId, volume)))
        {
            g_debug("set volume %d for display: %d", volume, displayId);
            --(volumeInstance->displayTwoVolume);
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
            pbnjson::JValue setVolumeResponse = pbnjson::Object();
            setVolumeResponse.put("returnValue", true);
            setVolumeResponse.put("volume", volume);
            setVolumeResponse.put("soundOutput", soundOutput);
            reply = setVolumeResponse.stringify();
        }
        else
        {
            g_debug("Did not able to set volume %d for display: %d", volume, displayId);
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_NOT_SUPPORT_VOLUME_CHANGE, "SoundOutput volume is not in range");
        }
        CLSError lserror;
        if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            lserror.Print(__FUNCTION__, __LINE__);
    }
    else
    {
        if ((volumeInstance->displayOneVolume-1) >= MIN_VOLUME)
        {
            isValidVolume = true;
            volume = volumeInstance->displayOneVolume-1;
        }
        else
        {
            g_debug("Volume down value not in range");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_NOT_SUPPORT_VOLUME_CHANGE, "SoundOutput volume is not in range");
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
                lserror.Print(__FUNCTION__, __LINE__);
        }
        if ((isValidVolume) && (audioMixerObj && audioMixerObj->setVolume(displayId, volume)))
        {
            g_debug("set volume %d for display: %d", volume, displayId);
            --(volumeInstance->displayOneVolume);
            volumeInstance->notifyVolumeSubscriber(displayId, callerId);
        }
        envelopeRef *envelope = new (std::nothrow)envelopeRef;
        if(nullptr != envelope)
        {
            envelope->message = message;
            envelope->context = (volumeSettings*)ctx;
            volumeSettings *volumeSettingsObj = (volumeSettings*)ctx;
            if ((nullptr != volumeSettingsObj->mixerObj) && (isValidVolume))
            {
                if(volumeSettingsObj->mixerObj->masterVolumeDown(soundOutput, _volumeDownCallBack, envelope))
                {
                    g_debug("MasterVolume: masterVolumeDown umimixer call successfull");
                    LSMessageRef(message);
                    status = true;
                }
                else
                {
                    g_debug("MasterVolume: masterVolumeDown umimixer call failed");
                    reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_FAILED_MIXER_CALL, "Internal error");
                }
            }
            else
            {
                g_debug("MasterVolume: gumiaudiomixer is NULL");
                reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_MIXER_INSTANCE, "Internal error");
            }
        }
        else
        {
            g_debug("MasterVolume: masterVolumeDown envelope is NULL");
            reply = STANDARD_JSON_ERROR(AUDIOD_ERRORCODE_INVALID_ENVELOPE_INSTANCE , "Internal error");
        }
        if (false == status)
        {
            CLSError lserror;
            if (!LSMessageReply(lshandle, message, reply.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            if (nullptr != envelope)
            {
                delete envelope;
                envelope = nullptr;
            }
        }
    }
    return true;
}

bool volumeSettings::_volumeDownCallBack(LSHandle *sh, LSMessage *reply, void *ctx)
{
    g_debug("MasterVolume: volumeDownCallBack");
    std::string payload = LSMessageGetPayload(reply);
    JsonMessageParser ret(payload.c_str(), NORMAL_SCHEMA(PROPS_1(PROP(returnValue, boolean)) REQUIRED_1(returnValue)));
    bool returnValue = false;
    if (ret.parse(__FUNCTION__))
    {
        ret.get("returnValue", returnValue);
    }
    std::string soundOutput;
    int iVolume = 0;
    if (returnValue)
    {
        JsonMessageParser data(payload.c_str(), NORMAL_SCHEMA(PROPS_2(PROP(volume, integer),\
            PROP(soundOutput, string)) REQUIRED_2(soundOutput, volume)));
        if (data.parse(__FUNCTION__))
        {
            data.get("soundOutput", soundOutput);
            data.get("volume", iVolume);
            g_debug("MasterVolume::Successfully decreased the speaker volume for sound out %s with volume %d", soundOutput.c_str(), iVolume);
        }
        else
        {
            returnValue = false;
        }
    }
    else
    {
        g_debug("MasterVolume: Could not volume up");
    }
    if (nullptr != ctx)
    {
        envelopeRef *envelope  = (envelopeRef*)ctx;
        LSMessage *message = (LSMessage*)envelope->message;
        volumeSettings *volumeSettingsObj = (volumeSettings*)envelope->context;
        if(true == returnValue)
        {
            if (nullptr != volumeSettingsObj)
            {
                volumeSettingsObj->setCurrentVolume(iVolume);
            }
        }
        if (nullptr != message)
        {
            CLSError lserror;
            if (!LSMessageRespond(message, payload.c_str(), &lserror))
            {
                lserror.Print(__FUNCTION__, __LINE__);
            }
            LSMessageUnref(message);
        }
        else
        {
            g_debug("MasterVolume: internal mixer call");
        }
        if (nullptr != envelope)
        {
            delete envelope;
            envelope = nullptr;
        }
    }
    else
    {
        g_debug("MasterVolume: context is null");
    }
    return true;
}


void volumeSettings::setCurrentVolume(int iVolume)
{
    mVolume = iVolume;
    g_debug("MasterVolume::updated volume: %d ", mVolume);
}

void volumeSettings::setCurrentMuteStatus(bool bMuteStatus)
{
    mMuteStatus = bMuteStatus;
    g_debug("MasterVolume::updated mute status: %d ", (int)mMuteStatus);
}

/* TODO
currently these luna API's are not in sync with exsting master volume
In future existing master volume will be removed and these below luna API's will be used
for master volume handling as per Generic AV arch and made in sync with remote and other services*/

static LSMethod MasterVolumeMethods[] =
{
    {cModuleMethod_SetVolume, volumeSettings::_setVolume},
    {cModuleMethod_VolumeDown, volumeSettings::_volumeDown},
    {cModuleMethod_VolumeUp, volumeSettings::_volumeUp},
    {cModuleMethod_GetVolume, volumeSettings::_getVolume},
    {cModuleMethod_muteVolume, volumeSettings::_muteVolume},
    { },
};
volumeSettings::volumeSettings(): mixerObj(AudioMixer::getAudioMixerInstance()), mVolume(0), mMuteStatus(false), displayOneVolume(100), displayTwoVolume(100)
{
    g_debug("volumeSettings: constructor");
}
volumeSettings::~volumeSettings()
{
    g_debug("volumeSettings: destructor");
}

volumeSettings* volumeSettings::getVolumeSettingsInstance()
{
    static volumeSettings volumeSettingsInstance;
    return &volumeSettingsInstance;
}

int masterVolumeInit(GMainLoop *loop, LSHandle *handle)
{
    g_debug("masterVolumeInit");
    bool bRetVal = false;
    LSError lSError;
    LSErrorInit(&lSError);
    volumeSettings* volumeInstance = volumeSettings::getVolumeSettingsInstance();
    if (nullptr == volumeInstance)
    {
        return (-1);
    }
    bRetVal = LSRegisterCategoryAppend(handle, "/master", MasterVolumeMethods, nullptr, &lSError);

    if (!bRetVal || !LSCategorySetData(handle, "/master", volumeInstance, &lSError))
    {
        g_message("%s: Registering Service for '%s' category failed", __FUNCTION__, "/master");
        LSErrorPrint(&lSError, stderr);
        LSErrorFree(&lSError);
        return (-1);
    }
    g_debug("MasterVolumeInit Done");
    return 0;
}
MODULE_START_FUNC(masterVolumeInit);
