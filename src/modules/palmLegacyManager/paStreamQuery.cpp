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

#include "paStreamQuery.h"

static std::string streamClass(const pa_proplist *props)
{
    const char *type = props ? pa_proplist_gets(props, "media.type") : nullptr;
    return type ? type : "";
}

bool PaStreamQuery::start(pa_context *context)
{
    cancel();
    mStreams.clear();
    mOps[0] = pa_context_get_sink_input_info_list(context, &PaStreamQuery::inputCb, this);
    mOps[1] = pa_context_get_source_output_info_list(context, &PaStreamQuery::outputCb, this);
    if (!mOps[0] || !mOps[1])
    {
        cancel();
        return false;
    }
    return true;
}

void PaStreamQuery::cancel()
{
    for (pa_operation *&op : mOps)
    {
        if (op)
        {
            pa_operation_cancel(op);
            pa_operation_unref(op);
            op = nullptr;
        }
    }
}

void PaStreamQuery::inputCb(pa_context *, const pa_sink_input_info *info, int eol, void *userdata)
{
    PaStreamQuery *self = static_cast<PaStreamQuery*>(userdata);
    if (eol || !info)
    {
        self->listed(0, eol);
        return;
    }
    SuspendVoter::Stream s;
    s.corked = info->corked;
    s.client = info->client != PA_INVALID_INDEX;
    s.bufferUsec = info->buffer_usec;
    s.sink = streamClass(info->proplist);
    self->mStreams.push_back(std::move(s));
}

void PaStreamQuery::outputCb(pa_context *, const pa_source_output_info *info, int eol, void *userdata)
{
    PaStreamQuery *self = static_cast<PaStreamQuery*>(userdata);
    if (eol || !info)
    {
        self->listed(1, eol);
        return;
    }
    SuspendVoter::Stream s;
    s.capture = true;
    s.corked = info->corked;
    s.client = info->client != PA_INVALID_INDEX;
    s.sink = streamClass(info->proplist);
    self->mStreams.push_back(std::move(s));
}

void PaStreamQuery::listed(size_t which, int eol)
{
    pa_operation_unref(mOps[which]);
    mOps[which] = nullptr;

    if (eol < 0)
    {
        cancel();
        mDone(false, mStreams);
    }
    else if (!mOps[0] && !mOps[1])
        mDone(true, mStreams);
}

bool PaStreamQuery::hold(pa_context *context, bool hold, pa_context_success_cb_t done, void *userdata)
{
    bool asked = true;
    for (pa_operation *op : {pa_context_suspend_sink_by_index(context, PA_INVALID_INDEX, hold,
                                                              done, userdata),
                             pa_context_suspend_source_by_index(context, PA_INVALID_INDEX, hold,
                                                                done, userdata)})
    {
        if (op)
            pa_operation_unref(op);
        else
            asked = false;
    }
    return asked;
}
