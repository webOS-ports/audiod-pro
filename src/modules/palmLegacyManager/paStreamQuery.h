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

#ifndef _PA_STREAM_QUERY_H
#define _PA_STREAM_QUERY_H

#include <array>
#include <functional>
#include <vector>

#include <pulse/pulseaudio.h>

#include "suspendVoter.h"

/*
 * One look at every playback and capture stream PulseAudio has, for
 * SuspendVoter: two listings, whose results come together once both are done.
 * Cancelled (and destroyed) before the context it runs on goes.
 */
class PaStreamQuery
{
public:
    //! ok false: PulseAudio refused a listing.
    using Done = std::function<void(bool ok, const std::vector<SuspendVoter::Stream> &streams)>;

    explicit PaStreamQuery(Done done) : mDone(std::move(done)) {}
    ~PaStreamQuery() { cancel(); }
    PaStreamQuery(const PaStreamQuery&) = delete;
    PaStreamQuery& operator=(const PaStreamQuery&) = delete;

    bool start(pa_context *context);
    void cancel();

    //! Suspend every sink and source, or let them go again. False when
    //! PulseAudio cannot be asked.
    static bool hold(pa_context *context, bool hold, pa_context_success_cb_t done, void *userdata);

private:
    static void inputCb(pa_context *c, const pa_sink_input_info *info, int eol, void *userdata);
    static void outputCb(pa_context *c, const pa_source_output_info *info, int eol, void *userdata);
    void listed(size_t which, int eol);

    Done mDone;
    std::array<pa_operation*, 2> mOps{};
    std::vector<SuspendVoter::Stream> mStreams;
};

#endif // _PA_STREAM_QUERY_H
