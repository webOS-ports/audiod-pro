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

#include "voiceVolumeStream.h"

#include <cmath>

namespace
{
    /* The same everywhere: mono, the smallest format there is. Nothing is
     * ever written, the stream only carries a volume. */
    const pa_sample_spec kSpec = { PA_SAMPLE_S16LE, 8000, 1 };

    pa_cvolume volumeOf(double linear)
    {
        pa_cvolume cv;
        pa_cvolume_set(&cv, kSpec.channels, pa_sw_volume_from_linear(linear));
        return cv;
    }
}

VoiceVolumeStream::~VoiceVolumeStream()
{
    drop();
}

std::vector<std::pair<std::string, std::string>> VoiceVolumeStream::properties(const std::string &sink)
{
    return {
        /* What pulseaudio-modules-droid takes the voice volume from. */
        { PA_PROP_MEDIA_ROLE, "phone" },
        { PA_PROP_MEDIA_NAME, "Call volume" },
        { PA_PROP_APPLICATION_NAME, "audiod" },
        /* module-palm-policy leaves a stream that names its device on the
         * sink it was opened on, and tracks it as no class of its own. */
        { "preferred.device", sink },
        /* Not "sink-input-by-media-role:phone", which every phone-role
         * stream without a volume of its own would be restored to. */
        { "module-stream-restore.id", "audiod-call-volume" },
    };
}

bool VoiceVolumeStream::apply(pa_context *context, const std::string &sink, double linear)
{
    if (!context || sink.empty())
        return false;

    if (mStream && mSink != sink)
        drop();

    mLinear = linear;

    if (mStream)
    {
        if (pa_stream_get_state(mStream) == PA_STREAM_READY)
            setVolume(context);
        /* Still connecting: stateCb() sets it once it is ready. */
        return true;
    }

    pa_proplist *props = pa_proplist_new();
    if (!props)
        return false;
    for (const auto &[key, value] : properties(sink))
        pa_proplist_sets(props, key.c_str(), value.c_str());
    mStream = pa_stream_new_with_proplist(context, "Call volume", &kSpec, nullptr, props);
    pa_proplist_free(props);
    if (!mStream)
        return false;

    mSink = sink;
    pa_stream_set_state_callback(mStream, &VoiceVolumeStream::stateCb, this);

    const pa_cvolume cv = volumeOf(linear);
    const pa_stream_flags_t flags = static_cast<pa_stream_flags_t>(
        PA_STREAM_START_CORKED | PA_STREAM_DONT_MOVE);
    if (pa_stream_connect_playback(mStream, sink.c_str(), nullptr, flags, &cv, nullptr) < 0)
    {
        drop();
        return false;
    }
    mApplied = linear;
    return true;
}

void VoiceVolumeStream::drop()
{
    if (!mStream)
        return;

    pa_stream *stream = mStream;
    mStream = nullptr;
    mSink.clear();
    mApplied = -1.0;
    pa_stream_set_state_callback(stream, nullptr, nullptr);
    if (PA_STREAM_IS_GOOD(pa_stream_get_state(stream)))
        (void) pa_stream_disconnect(stream);
    pa_stream_unref(stream);
}

void VoiceVolumeStream::setVolume(pa_context *context)
{
    if (!mStream || !context || std::fabs(mApplied - mLinear) < 1e-9)
        return;

    const uint32_t index = pa_stream_get_index(mStream);
    if (index == PA_INVALID_INDEX)
        return;

    const pa_cvolume cv = volumeOf(mLinear);
    pa_operation *op = pa_context_set_sink_input_volume(context, index, &cv, nullptr, nullptr);
    if (op)
    {
        pa_operation_unref(op);
        mApplied = mLinear;
    }
}

void VoiceVolumeStream::stateCb(pa_stream *stream, void *userdata)
{
    VoiceVolumeStream *self = static_cast<VoiceVolumeStream*>(userdata);
    if (!self || self->mStream != stream)
        return;

    switch (pa_stream_get_state(stream))
    {
        case PA_STREAM_READY:
            /* A volume asked for while it was connecting. */
            self->setVolume(pa_stream_get_context(stream));
            break;
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            /* The sink went (a profile switch, PulseAudio going away): the
             * next apply() opens it again. */
            self->drop();
            break;
        default:
            break;
    }
}
