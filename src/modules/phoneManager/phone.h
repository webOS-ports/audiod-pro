// Copyright (c) 2012-2018 LG Electronics, Inc.
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

#ifndef _PHONE_H_
#define _PHONE_H_

#include "moduleInterface.h"
#include "audioMixer.h"

class PhoneScenarioModule : public ModuleInterface
{
public:
    PhoneScenarioModule();

    virtual void    programHardwareVolume();
    virtual void    programCallertoneVolume(bool ramp);
    virtual void    programControlVolume();
    virtual void    programMuted();
    virtual void    programMuse(bool enable);
    virtual void    programHac (bool enable);
//    virtual void    onSinkChanged(EVirtualAudioSink sink, EControlEvent event, ESinkType p_eSinkType);

    virtual void    onActivating();
    virtual void    onDeactivated();

    virtual int        adjustAlertVolume(int volume, bool alertStarting = false);

protected:
    int            mFrontSpeakerVolume;
    int            mBackSpeakerVolume;
    int            mHeadsetVolume;
    int            mBluetoothVolume;
    int            mTTYFullVolume;
    int            mTTYHCOVolume;
    int            mTTYVCOVolume;
};


PhoneScenarioModule * getPhoneModule();

#endif // _PHONE_H_
