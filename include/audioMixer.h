/* @@@LICENSE
*
*      Copyright (c) 2020-2022 LG Electronics Company.
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

#ifndef _AUDIO_MIXER_H_
#define _AUDIO_MIXER_H_

#include "utils.h"
#include "messageUtils.h"
#include "PulseAudioMixer.h"
#include "umiaudiomixer.h"
#include "moduleManager.h"
#include "main.h"
#include <cstdlib>
#include <bits/stdc++.h>

class AudioMixer : public MixerInterface
{
    private:
        AudioMixer(const AudioMixer&) = delete;
        AudioMixer& operator=(const AudioMixer&) = delete;
        AudioMixer();
        umiaudiomixer* mObjUmiAudioMixer;
        PulseAudioMixer* mObjPulseAudioMixer;
        ModuleManager *mObjModuleManager;
        utils::vectorVirtualSink mActiveStreams;
        utils::vectorVirtualSource mActiveSources;
        utils::vectorVirtualSink mUmiStreams;
        utils::vectorVirtualSink mPulseStreams;
        bool mUmiMixerStatus;
        bool mPulseMixerStatus;

        //utility functions used within audio mixer class
        void addAudioSink(EVirtualAudioSink audioSink, utils::EMIXER_TYPE mixerType);
        void removeAudioSink(EVirtualAudioSink audioSink, utils::EMIXER_TYPE mixerType);
        void addAudioSource(EVirtualSource audioSource, utils::EMIXER_TYPE mixerType);
        void removeAudioSource(EVirtualSource audioSource, utils::EMIXER_TYPE mixerType);

        void resetStreamInfo(utils::EMIXER_TYPE mixerType);

    public:
        ~AudioMixer();
        static AudioMixer* getAudioMixerInstance();
        bool readyToProgram();
        bool getPulseMixerReadyStatus();
        bool getUmiMixerReadyStatus();

        //umiAudioMixer calls
        bool connectAudio(const std::string &strSourceName, const std::string &strPhysicalSinkName, LSFilterFunc cb, envelopeRef *message);
        bool disconnectAudio(const std::string &strSourceName, const std::string &strPhysicalSinkName, LSFilterFunc cb, envelopeRef *message);
        bool setSoundOut(const std::string &strOutputMode, LSFilterFunc cb, envelopeRef *message);
        bool setMasterVolume(const std::string &strSoundOutPut, const int &iVolume, LSFilterFunc cb, envelopeRef *message);
        bool getMasterVolume(LSFilterFunc cb, envelopeRef *message);
        bool masterVolumeUp(const std::string &strSoundOutPut, LSFilterFunc cb, envelopeRef *message);
        bool masterVolumeDown(const std::string &strSoundOutPut, LSFilterFunc cb, envelopeRef *message);
        bool masterVolumeMute(const std::string &strSoundOutPut, const bool &bIsMute, LSFilterFunc cb, envelopeRef *message);
        bool inputVolumeMute(const std::string &strPhysicalSink, const std::string &strSource, const bool &bIsMute, LSFilterFunc cb, envelopeRef *message);
        bool getConnectionStatus(LSFilterFunc cb, envelopeRef *message);
        bool onSinkChangedReply(const std::string& source, const std::string& sink, EVirtualAudioSink eVirtualSink,\
                                utils::ESINK_STATUS eSinkStatus, utils::EMIXER_TYPE eMixerType);

        //To know audiooutputd server status - Need to implement from adapter class
        //static bool audiodOutputdServiceStatusCallBack(LSHandle *sh, const char *serviceName, bool connected, void *ctx);

        //pulseAudioMixer calls
        bool programVolume(EVirtualAudioSink sink, int volume, bool ramp = false);
        bool programTrackVolume(EVirtualAudioSink sink, int sinkIndex, int volume, bool ramp = false);
        bool programVolume(EVirtualSource source, int volume, bool ramp = false);
        bool programCallVoiceOrMICVolume(char cmd, int volume);
        bool programMute(EVirtualSource source, int mute);
        bool rampVolume(EVirtualAudioSink sink, int endVolume);
        bool moveInputDeviceRouting(EVirtualSource source, const char* deviceName);
        bool setSoundOutputOnRange(EVirtualAudioSink startSink,\
            EVirtualAudioSink endSink, const char* deviceName);
        bool setSoundInputOnRange(EVirtualSource startSource,\
            EVirtualSource endSource, const char* deviceName);
        bool setDefaultSinkRouting(EVirtualAudioSink startSink, EVirtualAudioSink endSink);
        bool setDefaultSourceRouting(EVirtualSource startSource, EVirtualSource endSource);

        bool programFilter(int filterTable);
        bool muteAll();
        bool suspendAll();
        bool updateRate(int rate);
        bool setPhysicalSourceMute(const char* source, const int& mutestatus);
        bool muteSink(const int& sink, const int& mutestatus);
        bool setVirtualSourceMute(int sink, int mutestatus);

        bool setMute(const char* deviceName, const int& mutestatus);
        bool setVolume(const char* deviceName, const int& volume);
        bool setMicVolume(const char* deviceName, const int& volume);
        bool playSystemSound(const char *snd, EVirtualAudioSink sink);
        bool playSound(const char *snd, EVirtualAudioSink sink, \
            const char *format, int rate, int channels);
        bool programHeadsetRoute(EHeadsetState route);
        bool externalSoundcardPathCheck(std::string filename,  int status);
        bool loadUSBSinkSource(char cmd,int cardno, int deviceno, int status);
        bool sendUsbMultipleDeviceInfo(int isOutput, int maxDeviceCount, const std::string &deviceBaseName);
        bool sendInternalDeviceInfo(int isOutput, int maxDeviceCount);
        bool loadInternalSoundCard(char cmd, int cardno, int deviceno, int status,bool isOutput, const char* deviceName);
        bool _connectSocket();
        bool suspendSink(int sink);
        bool programLoadBluetooth(const char * address , const char *profile, const int displayID);
        bool programUnloadBluetooth(const char *profile, const int displayID);
        bool programA2dpSource (const bool& a2dpSource);
        bool setRouting(const ConstString & scenario);
        bool programSource(char cmd, int sink, int value);

        void outputStreamOpened(EVirtualAudioSink sink, int sinkIndex, std::string trackId);
        void outputStreamClosed(EVirtualAudioSink sink, int sinkIndex, std::string trackId);
        void inputStreamOpened(EVirtualSource source);
        void inputStreamClosed(EVirtualSource source);
        void preloadSystemSound(const char * snd);
        void playOneshotDtmf(const char *snd, EVirtualAudioSink sink) ;
        void playOneshotDtmf(const char *snd, const char* sink) ;
        void playDtmf(const char *snd, EVirtualAudioSink sink) ;
        void playDtmf(const char *snd, const char* sink) ;
        void stopDtmf();
        void _pulseStatus(GIOChannel * ch, GIOCondition condition, gpointer user_data);
        void _timer();
        void setNREC(bool value);
        void openCloseSink(EVirtualAudioSink sink, bool openNotClose, int sinkIndex, std::string trackId);
        int loopback_set_parameters(const char * value);
        bool closeClient(int sinkIndex);

        //Audio mixer calls
        utils::vectorVirtualSink getActiveStreams();
        bool isStreamActive(EVirtualAudioSink sink);
        utils::vectorVirtualSource getActiveSources();
        bool isSourceActive(EVirtualSource source);

        //mixer interface implementation
        void callBackSinkStatus(const std::string& source, const std::string& sink, EVirtualAudioSink audioSink, \
              utils::ESINK_STATUS sinkStatus, utils::EMIXER_TYPE mixerType,const int& sinkIndex = -1, const std::string& trackId="");
        void callBackMixerStatus(const bool& mixerStatus, utils::EMIXER_TYPE mixerType);
        void callBackSourceStatus(const std::string& source, const std::string& sink, EVirtualSource audioSource, \
            utils::ESINK_STATUS sourceStatus, utils::EMIXER_TYPE mixerType);
        void callBackDeviceConnectionStatus(const std::string &deviceName, const std::string &deviceNameDetail, utils::E_DEVICE_STATUS deviceStatus, utils::EMIXER_TYPE mixerType);
        void callBackMasterVolumeStatus();

};

#endif //_AUDIO_MIXER_H_
