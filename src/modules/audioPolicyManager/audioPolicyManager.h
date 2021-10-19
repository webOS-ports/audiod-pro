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

#ifndef _AUDIO_POLICY_MGR_H_
#define _AUDIO_POLICY_MGR_H_

#include "utils.h"
#include "messageUtils.h"
#include "main.h"
#include <cstdlib>
#include "moduleInterface.h"
#include "moduleFactory.h"
#include "moduleManager.h"
#include "volumePolicyInfoParser.h"
#include "audioMixer.h"

#define VOLUME_POLICY_CONFIG "audiod_sink_volume_policy_config.json"
#define SOURCE_VOLUME_POLICY_CONFIG "audiod_source_volume_policy_config.json"

class AudioPolicyManager : public ModuleInterface
{
    private:
        AudioPolicyManager(const AudioPolicyManager&) = delete;
        AudioPolicyManager& operator=(const AudioPolicyManager&) = delete;
        ModuleManager* mObjModuleManager;
        VolumePolicyInfoParser* mObjPolicyInfoParser;
        AudioMixer* mObjAudioMixer;
        std::vector<utils::VOLUME_POLICY_INFO_T> mVolumePolicyInfo;
        std::vector<utils::VOLUME_POLICY_INFO_T> mSourceVolumePolicyInfo;
        std::vector<utils::APP_VOLUME_INFO_T> mVectAppVolumeInfo;
        utils::mapSinkToStream mSinkToStream;
        utils::mapStreamToSink mStreamToSink;
        utils::mapSourceToStream mSourceToStream;
        utils::mapStreamToSource mStreamToSource;
        utils::mapAppVolumeInfo mAppVolumeInfo;
        static bool mIsObjRegistered;
        AudioPolicyManager(ModuleConfig* const pConfObj);
        //Register Object to object factory. This is called automatically
        static bool RegisterObject()
        {
            return (ModuleFactory::getInstance()->Register("load_audio_policy_manager", &AudioPolicyManager::CreateObject));
        }
        void readPolicyInfo();
        void printPolicyInfo();
        void printActivePolicyInfo();
        void createSinkStreamMap(const pbnjson::JValue& policyInfo);
        void createSourceStreamMap(const pbnjson::JValue& policyInfo);

        void updateCurrentVolume(const std::string& streamType, const int &volume);
        void updateCurrentVolumeForSource(const std::string& streamType, const int &volume);
        void updateMuteStatus(const std::string& streamType, const bool &mute);
        void updateMuteStatusForSource(const std::string& streamType, const bool &mute);

        int getCurrentSinkMuteStatus(const std::string &streamType);
        int getCurrentSourceMuteStatus(const std::string &streamType);

        void addSinkInput(const std::string &appName, const int &sinkIndex, const std::string& sink);
        void removeSinkInput(const std::string &appName, const int &sinkIndex, const std::string& sink);

        void applyVolumePolicy(EVirtualAudioSink audioSink, const std::string& streamType, const int& priority);
        void applyVolumePolicy(EVirtualSource audioSource, const std::string& streamType, const int& priority);
        void removeVolumePolicy(EVirtualAudioSink audioSink, const std::string& streamType, const int& priority);
        void removeVolumePolicy(EVirtualSource audioSource, const std::string& streamType, const int& priority);
        void updatePolicyStatus(const std::string& streamType, const bool& status);
        void updatePolicyStatusForSource(const std::string& streamType, const bool& status);
        void initStreamVolume();
        bool setVolume(EVirtualAudioSink audioSink, const int &volume, utils::EMIXER_TYPE mixerType, bool ramp = false);
        bool setAppVolume(const std::string& mediaId, const int &volume, EVirtualAudioSink audioSink, utils::EMIXER_TYPE mixerType, bool ramp = false);
        bool setVolume(EVirtualSource audioSource, const int &volume, utils::EMIXER_TYPE mixerType, bool ramp = false);
        bool storeAppVolume(const std::string &mediaId, const int &volume, EVirtualAudioSink audioSink);
        bool muteSink(EVirtualAudioSink audioSink, const int &muteStatus, utils::EMIXER_TYPE mixerType);
        bool initializePolicyInfo(const pbnjson::JValue& policyInfo, bool isSink);

        bool getPolicyStatus(const std::string& streamType);
        bool getPolicyStatusOfSource(const std::string& streamType);
        bool isHighPriorityStreamActive(const int& policyPriority, utils::vectorVirtualSink activeStreams, const std::string &category);
        bool isHighPrioritySourceActive(const int& policyPriority, utils::vectorVirtualSource activeStreams);
        bool isRampPolicyActive(const std::string& streamType);
        bool isRampPolicyActiveForSource(const std::string& streamType);
        bool getStreamActiveStatus(const std::string& streamType);
        bool getSourceActiveStatus(const std::string& streamType);
        int getPriority(const std::string& streamType);
        int getPriorityOfSource(const std::string& streamType);
        int getPolicyVolume(const std::string& streamType);
        int getPolicyVolumeofSource(const std::string& streamType);
        int getCurrentVolume(const std::string& streamType);
        int getCurrentVolumeOfSource(const std::string& streamType);
        utils::EMIXER_TYPE getMixerType(const std::string& streamType);
        utils::EMIXER_TYPE getMixerTypeofSource(const std::string& streamType);
        EVirtualAudioSink getSinkType(const std::string& streamType);
        EVirtualSource getSourceType(const std::string& streamType);
        std::string getStreamType(EVirtualAudioSink audioSink);
        std::string getStreamType(EVirtualSource audioSource);
        std::string getCategory(const std::string& streamType);
        std::string getCategoryOfSource(const std::string& streamType);

    public:
        ~AudioPolicyManager();
        static AudioPolicyManager* getAudioPolicyManagerInstance();
        static AudioPolicyManager *mAudioPolicyManager;
        static ModuleInterface* CreateObject(ModuleConfig* const pConfObj)
        {
            if (mIsObjRegistered)
            {
                PM_LOG_DEBUG("CreateObject - Creating the AudioPolicyManager handler");
                mAudioPolicyManager = new(std::nothrow) AudioPolicyManager(pConfObj);
                if (mAudioPolicyManager)
                    return mAudioPolicyManager;
            }
            return nullptr;
        }
        void initialize();
        void deInitialize();
        void handleEvent(events::EVENTS_T *event);

        //luna api's
        static bool _setInputVolume(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _setSourceInputVolume(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _getInputVolume(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _getSourceInputVolume(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _getStreamStatus(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _getSourceStatus(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _muteSource(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _muteSink(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _setAppVolume(LSHandle *lshandle, LSMessage *message, void *ctx);
        static bool _registerTrack(LSHandle *lshandle, LSMessage *message, void *ctx);

        void notifyGetVolumeSubscribers(const std::string& streamType, const int& volume);
        void notifyGetSourceVolumeSubscribers(const std::string& streamType, const int& volume);
        void notifyGetStreamStatusSubscribers(const std::string& payload) const;
        void notifyGetSourceStatusSubscribers(const std::string& payload) const;
        void printAppVolumeInfo();
        static bool _setMediaInputVolume(LSHandle *lshandle, LSMessage *message, void *ctx);

        void eventSinkStatus(const std::string& source, const std::string& sink, EVirtualAudioSink audioSink, \
            utils::ESINK_STATUS sinkStatus, utils::EMIXER_TYPE mixerType,const int& sinkIndex = -1, const std::string& appName="");
        void eventSourceStatus(const std::string& source, const std::string& sink, EVirtualSource audioSource, \
            utils::ESINK_STATUS sourceStatus, utils::EMIXER_TYPE mixerType);

        void eventMixerStatus(bool mixerStatus, utils::EMIXER_TYPE mixerType);
        void eventCurrentInputVolume(EVirtualAudioSink audioSink, const int& volume);
        void notifyInputVolume(EVirtualAudioSink audioSink, const int& volume, const bool& ramp);
        std::string getStreamStatus(const std::string& streamType, bool subscribed);
        std::string getSourceStatus(const std::string& streamType, bool subscribed);
        std::string getStreamStatus(bool subscribed);
        std::string getSourceStatus(bool subscribed);
};

#define AUDIOD_UNIQUE_ID_LENGTH 10

class GenerateUniqueID {
    const std::string           source_;
    const int                   base_;
    const std::function<int()>  rand_;

    public:
    GenerateUniqueID(GenerateUniqueID&) = delete;
    GenerateUniqueID& operator= (const GenerateUniqueID&) = delete;

    explicit
    GenerateUniqueID(const std::string& src = "0123456789ABCDEFGIJKLMNOPQRSTUVWXYZabcdefgijklmnopqrstuvwxyz") :
        source_(src),
        base_(source_.size()),
        rand_(std::bind(
            std::uniform_int_distribution<int>(0, base_ - 1),
            std::mt19937( std::random_device()() )
        ))
    { }

    std::string operator ()()
    {
        struct timespec time;
        std::string s(AUDIOD_UNIQUE_ID_LENGTH, '0');

        clock_gettime(CLOCK_MONOTONIC, &time);

        s[0] = '_'; // Prepend uid with _ to comply with luna requirements
        for (int i = 1; i < AUDIOD_UNIQUE_ID_LENGTH; ++i) {
            if (i < 5 && i < AUDIOD_UNIQUE_ID_LENGTH - 6) {
                s[i] = source_[time.tv_nsec % base_];
                time.tv_nsec /= base_;
            } else if (time.tv_sec > 0 && i < AUDIOD_UNIQUE_ID_LENGTH - 3) {
                s[i] = source_[time.tv_sec % base_];
                time.tv_sec /= base_;
            } else {
                s[i] = source_[rand_()];
            }
        }

        return s;
    }
};
#endif //_AUDIO_POLICY_MGR_H_
