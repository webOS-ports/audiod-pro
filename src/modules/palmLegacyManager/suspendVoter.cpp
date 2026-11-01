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

#include "suspendVoter.h"

#include <cctype>

#include <pbnjson.hpp>

static bool isBrief(const std::string &sink)
{
    return sink == "pfeedback" || sink == "pDTMF";
}

SuspendVoter::Count SuspendVoter::count(const std::vector<Stream> &streams)
{
    Count c;
    for (const Stream &s : streams)
    {
        if (s.corked || !s.client)
            continue;
        if (isBrief(s.sink))
            ++c.brief;
        else if (!s.capture && s.bufferUsec < kDrainedUsec)
            ++c.drained;
        else
        {
            ++c.playing;
            ++c.sinks[s.sink.empty() ? "unnamed" : s.sink];
        }
    }
    return c;
}

std::string SuspendVoter::activityId(const std::string &sink)
{
    std::string id = "com.palm.audiod.";
    if (sink.empty() || sink.size() > 32)
        return id + "stream";
    for (unsigned char c : sink)
    {
        if (!std::isalnum(c))
            return id + "stream";
    }
    return id + sink;
}

const char *SuspendVoter::roundName() const
{
    return mRound == Round::Request ? "suspend request" : "prepare suspend";
}

void SuspendVoter::sleepdUp()
{
    mClientId.clear();
    mHost.identify();
}

void SuspendVoter::sleepdDown()
{
    mClientId.clear();
    if (mVoting)
    {
        mVoting = false;
        mHost.cancelQuery();
        mHost.stopTimer(Timer::NextSample);
        mHost.stopTimer(Timer::Deadline);
    }
    /* Its resume will not come. */
    release();
}

void SuspendVoter::identified(const std::string &payload)
{
    const pbnjson::JValue reply = pbnjson::JDomParser::fromString(payload);
    std::string clientId;
    bool subscribed = false;
    if (!reply.isObject() || reply["subscribed"].asBool(subscribed) != CONV_OK || !subscribed ||
        reply["clientId"].asString(clientId) != CONV_OK || clientId.empty())
    {
        mClientId.clear();
        mHost.log(true, "sleepd did not take audiod as a client: " + payload);
        return;
    }

    mClientId = clientId;
    pbnjson::JObject request;
    request.put("clientId", mClientId);
    request.put("register", true);
    mHost.call("suspendRequestRegister", request.stringify());
    mHost.call("prepareSuspendRegister", request.stringify());
    mHost.log(false, "Voting on suspend with sleepd");
}

void SuspendVoter::begin(Round round)
{
    if (!registered())
        return;

    /* A question while one is open: sleepd has stopped waiting for it. */
    if (mVoting)
        answer(true, false);

    /* A new vote is a new attempt, so the last one's resume is not coming. */
    if (mHeld && round == Round::Request)
    {
        mHost.log(true, "A suspend attempt without a resume after the last one");
        release();
    }

    mVoting = true;
    mRound = round;
    mSamples = 0;

    if (mHost.inCall())
    {
        const uint64_t now = mHost.nowMs();
        if (!mCallSeen)
        {
            mCallSeen = true;
            mCallSince = now;
        }
        if (now - mCallSince < kCallLimitMs)
        {
            nack("call", "a call is up");
            return;
        }
        mHost.log(true, "In a call for hours according to what audiod was told: not refusing "
                        "suspend for it any longer");
    }
    else
        mCallSeen = false;

    mHost.startTimer(Timer::Deadline, kDeadlineMs);
    sample();
}

void SuspendVoter::sample()
{
    ++mSamples;
    if (!mHost.queryStreams())
    {
        mHost.log(false, std::string("ACKing sleepd '") + roundName() +
                         "': PulseAudio cannot be asked");
        answer(true, false);
    }
}

void SuspendVoter::streams(const std::vector<Stream> &streams)
{
    if (!mVoting)
        return;

    const Count c = count(streams);
    if (c.playing)
    {
        std::string why;
        for (const auto &[sink, n] : c.sinks)
            why += (why.empty() ? "" : ", ") + std::to_string(n) + " on " + sink;
        nack(c.sinks.begin()->first, why);
        return;
    }

    if (c.drained && mSamples < kSamples)
    {
        mHost.startTimer(Timer::NextSample, kSampleGapMs);
        return;
    }

    if (c.drained)
        mHost.log(false, std::to_string(c.drained) + " uncorked streams have had nothing to play "
                         "for 2 s: not counted, continue with " + roundName());
    if (c.brief)
        mHost.log(false, std::to_string(c.brief) + " feedback streams open, continue with " +
                         roundName() + " since these streams should be very short");
    answer(true, true);
}

void SuspendVoter::streamsFailed()
{
    if (!mVoting)
        return;
    mHost.log(true, std::string("ACKing sleepd '") + roundName() +
                    "': PulseAudio did not list the streams");
    answer(true, false);
}

void SuspendVoter::nack(const std::string &sink, const std::string &why)
{
    /* The activity first: with it sleepd leaves this alone for kActivityMs. */
    pbnjson::JObject activity;
    activity.put("id", activityId(sink));
    activity.put("duration_ms", static_cast<int32_t>(kActivityMs));
    mHost.call("activityStart", activity.stringify());

    mHost.log(false, std::string("NACKing sleepd '") + roundName() + "' because of " + why +
                     " & booked activity");
    answer(false, true);
}

void SuspendVoter::answer(bool ack, bool paAnswered)
{
    mVoting = false;
    mHost.cancelQuery();
    mHost.stopTimer(Timer::NextSample);
    mHost.stopTimer(Timer::Deadline);

    if (ack && mRound == Round::Prepare && paAnswered && !mHeld)
    {
        mHeld = true;
        mHost.holdAudio(true);
        mHost.startTimer(Timer::Hold, kHoldLimitMs);
    }

    pbnjson::JObject vote;
    vote.put("clientId", mClientId);
    vote.put("ack", ack);
    mHost.call(mRound == Round::Request ? "suspendRequestAck" : "prepareSuspendAck",
               vote.stringify());
}

void SuspendVoter::release()
{
    if (!mHeld)
        return;
    mHeld = false;
    mHost.stopTimer(Timer::Hold);
    mHost.holdAudio(false);
}

void SuspendVoter::resume()
{
    release();
}

void SuspendVoter::paReady()
{
    /* Also what an audiod that died holding them left: PulseAudio keeps a
     * suspend asked for until it is taken back, by anyone. */
    mHeld = false;
    mHost.stopTimer(Timer::Hold);
    mHost.holdAudio(false);
}

void SuspendVoter::paLost()
{
    /* The hold went with it. */
    mHeld = false;
    mHost.stopTimer(Timer::Hold);
    if (mVoting)
    {
        mHost.log(false, std::string("ACKing sleepd '") + roundName() +
                         "': PulseAudio went");
        answer(true, false);
    }
}

void SuspendVoter::timerFired(Timer timer)
{
    switch (timer)
    {
        case Timer::NextSample:
            if (mVoting)
                sample();
            break;
        case Timer::Deadline:
            if (mVoting)
            {
                mHost.log(true, std::string("ACKing sleepd '") + roundName() +
                                "': PulseAudio did not answer in time");
                answer(true, false);
            }
            break;
        case Timer::Hold:
            if (mHeld)
            {
                mHost.log(true, "No resume from sleepd: letting the audio go again");
                release();
            }
            break;
        case Timer::Count:
            break;
    }
}

void SuspendVoter::stop()
{
    if (mVoting)
        answer(true, false);
    release();
}
