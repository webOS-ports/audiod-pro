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

#ifndef _CALL_VOLUME_H
#define _CALL_VOLUME_H

#include <array>
#include <optional>
#include <string>

#include "storedSetting.h"

/*
 * The in-call volume, one for each place a call can be heard, kept apart
 * from the manager so it can be tested.
 *
 * During a call the volume keys move this, not the master volume that the
 * music, the ringtones and the alerts are heard at: a call turned up on the
 * earpiece no longer leaves the music at that level once it ends. Each route
 * keeps its own - the earpiece, the loudspeaker, a wired headset or
 * headphones, a Bluetooth hands-free headset - and the next call on that
 * route starts where the last one was left.
 *
 * That is what legacy webOS did: its phone scenario module had a volume per
 * scenario (phone_front_speaker, phone_back_speaker, phone_headset with
 * phone_headset_mic sharing it, phone_bluetooth_sco), the volume keys moved
 * the current scenario's while the phone module was current, and they were
 * kept in lunaprefs as com.palm.audio "phone_preferences". Android keeps
 * STREAM_VOICE_CALL per output device (volume_voice_earpiece, ...) the same
 * way.
 *
 * The volume is a percentage like every other one in audiod, but it moves in
 * whole levels: kLevels steps from 0 to 100. The phone's call loudness has
 * no finer steps than that (see halVoiceVolume()), so a smaller step would
 * be a key press that changes nothing. 0 is the quietest level, not
 * silence: a call is never lost to the volume keys (Android's voice call
 * volume does not go below 1 either). Muting the call is the phone app's
 * mute, which is the microphone's.
 */
class CallVolume
{
public:
    enum class Route
    {
        Earpiece,
        Speaker,
        Wired,      //!< headset or headphones in the jack
        Bluetooth,  //!< a hands-free headset's SCO link
    };
    static constexpr int kRoutes = 4;

    //! Steps from the quietest to the loudest level.
    static constexpr int kLevels = 5;
    static constexpr int kStep = 100 / kLevels;
    //! Where a route starts until it is set: the loudest level, the
    //! nearest to the call loudness from before there was a call volume
    //! (see halVoiceVolume()).
    static constexpr int kDefault = 100;

    //! The route's name in logs and the settingsservice key it is kept
    //! under (category "sound").
    static const char *name(Route route);
    static const char *settingKey(Route route);

    //! The volume a call on route is heard at.
    int volume(Route route) const;

    //! The stored volume was read; snapped to a level. True if volume()
    //! changed. A volume the user set since audiod started wins.
    bool stored(Route route, int volume);

    //! The user set route's volume; snapped to a level. True if it changed.
    bool set(Route route, int volume);

    //! The user moved route's volume by offset percent: by whole levels,
    //! at least one in that direction for any offset but 0. True if it
    //! changed.
    bool offset(Route route, int offset);

    //! The level nearest to volume, within 0..100.
    static int snap(int volume);

    //! What the audio HAL is given as the voice volume (set_voice_volume(),
    //! 0.0..1.0) for a call volume.
    //!
    //! MediaTek's HAL (MT6739, audio.primary.mt6739.so) does not scale the
    //! call by this number: AudioALSAStreamManager::setVoiceVolume() turns it
    //! into a gain index, (int)(v * 5) + 1, from 1 to 6, and picks the
    //! downlink gain for the route from its tuning tables with that. Each
    //! level is therefore sent as the middle of its index's range rather
    //! than its edge, which float rounding could tip into the index below.
    //! A HAL that does scale the call gets close to the percentage itself.
    static double halVoiceVolume(int volume);

private:
    static size_t index(Route route);

    std::array<StoredSetting<int>, kRoutes> mVolume{
        StoredSetting<int>(kDefault), StoredSetting<int>(kDefault),
        StoredSetting<int>(kDefault), StoredSetting<int>(kDefault)};
};

#endif // _CALL_VOLUME_H
