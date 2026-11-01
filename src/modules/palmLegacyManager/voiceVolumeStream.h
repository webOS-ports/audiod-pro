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

#ifndef _VOICE_VOLUME_STREAM_H
#define _VOICE_VOLUME_STREAM_H

#include <string>
#include <utility>
#include <vector>

#include <pulse/pulseaudio.h>

/*
 * The handle on a carrier call's loudness.
 *
 * A carrier call's sound goes from the modem to the earpiece, the
 * loudspeaker or the headset inside the audio HAL; none of it passes through
 * PulseAudio, so no sink or stream volume there changes it. The HAL's own
 * knob is set_voice_volume(). pulseaudio-modules-droid turns it, in the
 * voicecall profile, from the volume of a sink-input on the droid card's
 * primary sink whose media.role is "phone" (droid-sink.c,
 * voice_property_key/voice_property_value), and on nothing else. LuneOS has
 * no such stream, so the HAL was never told a voice volume and every call
 * was at its default.
 *
 * This is that stream: corked, never written to, connected to the primary
 * sink for as long as a carrier call is up, its volume the call volume.
 * Its preferred.device keeps module-palm-policy from moving it to a
 * virtual sink or counting it as one of its stream classes, and its own
 * module-stream-restore id keeps its volume out of what other phone-role
 * streams are restored to.
 */
class VoiceVolumeStream
{
public:
    VoiceVolumeStream() = default;
    ~VoiceVolumeStream();
    VoiceVolumeStream(const VoiceVolumeStream &) = delete;
    VoiceVolumeStream &operator=(const VoiceVolumeStream &) = delete;

    //! The properties the stream is opened with, sink its sink's name.
    static std::vector<std::pair<std::string, std::string>> properties(const std::string &sink);

    //! The stream on sink, at the HAL voice volume linear (0.0..1.0):
    //! opened if it is not there (or is on another sink), its volume set if
    //! it is. False if it could not be opened.
    bool apply(pa_context *context, const std::string &sink, double linear);

    //! Closed: no call any more, or the context is about to go.
    void drop();

    //! Whether the stream is there (connecting or connected).
    bool open() const { return mStream != nullptr; }

private:
    static void stateCb(pa_stream *stream, void *userdata);
    void setVolume(pa_context *context);

    pa_stream *mStream = nullptr;
    std::string mSink;
    double mLinear = -1.0;      //!< what the stream is to be at
    double mApplied = -1.0;     //!< what it was opened with or last set to
};

#endif // _VOICE_VOLUME_STREAM_H
