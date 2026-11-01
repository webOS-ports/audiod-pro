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

#ifndef _USB_RING_H
#define _USB_RING_H

#include <cstdint>
#include <string>
#include <vector>

#include "callState.h"

/*
 * A USB headset rings the way a wired headset does: the ringtones and the
 * alerts play on the loudspeaker and the headset together, so a call, an
 * alarm or a notification is heard with the headset out of the ears. The
 * music, and everything else, stays in the headset; a call stays on its own
 * route.
 *
 * Legacy webOS did it for the wired headset in hardware: while an alert or
 * a ringtone played, MediaScenarioModule switched the codec to
 * media_back_speaker_headset_route (Veer audiod 0x000385c8 _updateRouting
 * l.63, routing type 2 -> BroadwayDevice::setRouting 0x0004f5c4 l.187-191).
 * "An alert or a ringtone" is someAlertIsPlaying (0x0003886c l.14-20): with
 * the ringer switch on, palerts, pnotifications, pcalendar (mask 0x803) or
 * pringtones; and palarm always. LuneOS's jack does the same on the droid
 * card's speaker-and-wired port (RingState). A USB headset is a sink of its
 * own, so here it is a module-combine-sink with pcm_output and the
 * headset's sink as slaves, and the router sends those classes there while
 * the headset carries the audio. module-palm-policy routes pnotifications
 * and pcalendar as its alerts class and palarm as its ringtones class.
 *
 * This class is the policy and the bookkeeping of that sink's module, kept
 * apart from PulseAudio so it can be tested; PalmLegacyManager does what
 * next() says and reports back.
 */
class UsbRing
{
public:
    //! The combined sink's name.
    static const char *const SINK;
    //! The sink it plays on besides the headset.
    static const char *const SPEAKER;

    /* The stream classes (module-palm-policy EVirtualSink numbers) that play
     * on both outside a call: the ringtones (and so the alarm) always, the
     * alerts with the ringer switch on. In a call, none: a ring then is
     * call waiting, which stays in the ear, as for the jack. */
    static std::vector<int> classes(ECallMode mode, bool ringerOn);

    //! module-combine-sink's arguments for usbSink, "" for a name that cannot
    //! go into a module argument as it is.
    static std::string moduleArguments(const std::string &usbSink);

    enum class Step
    {
        None,
        Load,       //!< load the combined sink for wanted(): moduleArguments(wanted())
        Unload      //!< unload module(), moving what plays on it off first
    };

    //! The output the audio goes to now. The combined sink is wanted while
    //! it is a USB headset's sink.
    void outputIs(const std::string &sink);
    //! The USB headset's sink to combine with the loudspeaker, "" for none.
    const std::string &wanted() const { return mWant; }

    /* Connected to PulseAudio, and no combined sink of an earlier audiod
     * is left: nothing is loaded before (one is left over when audiod
     * restarts while PulseAudio keeps running). */
    void checked();

    //! What to do now.
    Step next() const;
    //! A Load from next() was sent.
    void loading();
    //! It is loaded, as module index.
    void loaded(uint32_t module);
    //! It could not be loaded. Not tried again for the same headset.
    void loadFailed();
    //! The Unload from next() was sent; the module is as good as gone.
    void unloading();
    //! PulseAudio went away, and the module with it.
    void paGone();

    //! The combined sink for wanted() is there: the classes can go to it.
    bool ready() const;
    //! The loaded module, for Unload.
    uint32_t module() const { return mModule; }

    static constexpr uint32_t NO_MODULE = UINT32_MAX;

private:
    std::string mWant;          //!< the headset to combine with, "" for none
    std::string mFor;           //!< the headset the module is (being) loaded for
    std::string mFailedFor;     //!< the headset loading failed for
    uint32_t mModule = NO_MODULE;
    bool mLoading = false;
    bool mChecked = false;
};

#endif
