// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#include "usbRing.h"

#include <pulse/module-palm-policy.h>

const char *const UsbRing::SINK = "ring_speaker_usb";
const char *const UsbRing::SPEAKER = "pcm_output";

std::vector<int> UsbRing::classes(ECallMode mode, bool ringerOn)
{
    if (mode != eCallMode_None)
        return {};
    if (ringerOn)
        return {ealerts, eringtones};
    return {eringtones};
}

static bool plainName(const std::string &name)
{
    if (name.empty())
        return false;
    for (const char ch : name)
    {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-';
        if (!ok)
            return false;
    }
    return true;
}

std::string UsbRing::moduleArguments(const std::string &usbSink)
{
    /* slaves= is a comma-separated list inside a space-separated argument
     * string: a name with a comma, a space or a quote in it would change
     * what is loaded. module-alsa-sink's names (usb_speaker0) have none. */
    if (!CallState::isUsbOutput(usbSink) || !plainName(usbSink))
        return std::string();
    return std::string("sink_name=") + SINK + " slaves=" + SPEAKER + "," + usbSink;
}

void UsbRing::outputIs(const std::string &sink)
{
    mWant = CallState::isUsbOutput(sink) ? sink : std::string();
    /* Another headset (or the same one plugged in again) gets a new try. */
    if (mWant != mFailedFor)
        mFailedFor.clear();
}

void UsbRing::checked()
{
    mChecked = true;
}

UsbRing::Step UsbRing::next() const
{
    if (!mChecked || mLoading)
        return Step::None;
    if (mModule != NO_MODULE && mFor != mWant)
        return Step::Unload;
    if (mModule == NO_MODULE && !mWant.empty() && mWant != mFailedFor &&
        !moduleArguments(mWant).empty())
        return Step::Load;
    return Step::None;
}

void UsbRing::loading()
{
    mLoading = true;
    mFor = mWant;
}

void UsbRing::loaded(uint32_t module)
{
    mLoading = false;
    mModule = module;
}

void UsbRing::loadFailed()
{
    mLoading = false;
    mFailedFor = mFor;
    mFor.clear();
}

void UsbRing::unloading()
{
    mModule = NO_MODULE;
    mFor.clear();
}

void UsbRing::paGone()
{
    mFor.clear();
    mFailedFor.clear();
    mModule = NO_MODULE;
    mLoading = false;
    mChecked = false;
}

bool UsbRing::ready() const
{
    return mChecked && !mLoading && mModule != NO_MODULE && !mWant.empty() && mFor == mWant;
}
