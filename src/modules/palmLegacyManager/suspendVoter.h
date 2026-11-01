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

#ifndef _SUSPEND_VOTER_H
#define _SUSPEND_VOTER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/*
 * audiod's vote on suspending, as legacy webOS had it.
 *
 * sleepd asks its clients twice before it suspends: suspendRequest, then
 * prepareSuspend. Any NACK keeps the device up; a client that says nothing is
 * waited for (30 s and 5 s) and then ignored. So this answers every question,
 * always and quickly, whatever state PulseAudio is in.
 *
 * What counts is PulseAudio's own view at that moment, asked afresh for every
 * vote: there is no count of open streams kept here to go stale. A stream
 * counts when a client's stream is uncorked and sound is really moving
 * through it:
 *
 *  - Corked does not count: a paused player, audiod's own Call volume stream.
 *  - Uncorked is not enough. A client that just stops writing leaves its
 *    stream uncorked for as long as it lives. Spotify paused is exactly that:
 *    gst-launch is held with SIGSTOP and pulsesink never corks. Such a stream
 *    has an empty buffer, so it gets kSamples looks over two seconds, and if
 *    it stays empty it is left out. A stream that is being fed shows at
 *    least its latency there (7.5 ms in a 10 ms stream); an empty one shows
 *    the resampler's delay, under 0.2 ms.
 *  - Streams that belong to a module do not count: the remapped pcm_output
 *    keeps one on the card for ever.
 *  - pfeedback and pDTMF do not count, as in legacy: they are over in a moment.
 *
 * Bluetooth, USB, the jack and the loudspeaker are all the same here: this
 * looks at the streams, not at where they go.
 *
 * A call counts by itself: a carrier call has no running stream in PulseAudio
 * at all. That is a second line. Today the phone app's inCall makes the display
 * manager report the display on, so sleepd does not even ask during a call.
 * inCall is latched state in audiod, so a NACK for it ends after kCallLimit.
 *
 * Everything else is what makes it safe:
 *  - A NACK books a sleepd activity of kActivityMs, which expires by itself,
 *    and sleepd does not ask again until it has (it polls every 500 ms).
 *  - PulseAudio gone, failing or not answering within kDeadlineMs is an ACK.
 *  - On the last ACK the sinks and sources are suspended (holdAudio) until
 *    sleepd says resume. module-suspend-on-idle does not cover this: it only
 *    suspends what has no uncorked stream on it, so with Spotify paused the
 *    card's PCM would go into the kernel's suspend running.
 *  - The hold ends with resume, with sleepd going, with PulseAudio coming
 *    (an audiod that died holding it), with the next vote, or after
 *    kHoldLimitMs awake.
 *
 * Kept apart from LS2 and PulseAudio so it can be tested. The same thread as
 * the Host: audiod's main loop.
 */
class SuspendVoter
{
public:
    enum class Round
    {
        Request,    //!< sleepd's suspendRequest
        Prepare     //!< sleepd's prepareSuspend, the last question
    };

    enum class Timer
    {
        NextSample, //!< kSampleGapMs: look at the streams again
        Deadline,   //!< kDeadlineMs: answer sleepd whatever has happened
        Hold,       //!< kHoldLimitMs: resume never came
        Count
    };

    //! A stream as PulseAudio sees it.
    struct Stream
    {
        bool capture = false;
        bool corked = false;
        bool client = false;        //!< it has a client, it is not a module's
        uint64_t bufferUsec = 0;    //!< playback: what is waiting to be played
        std::string sink;           //!< media.type: its class, pdefaultapp, ...
    };

    class Host
    {
    public:
        virtual ~Host() = default;
        //! Identify with sleepd; the reply goes to identified().
        virtual void identify() = 0;
        //! Call a method of sleepd's /com/palm/power, without a reply.
        virtual void call(const char *method, const std::string &payload) = 0;
        //! Ask PulseAudio for the streams: the answer goes to streams() or
        //! streamsFailed(). False: PulseAudio cannot be asked.
        virtual bool queryStreams() = 0;
        virtual void cancelQuery() = 0;
        //! Suspend every sink and source, or let them go again.
        virtual void holdAudio(bool hold) = 0;
        virtual bool inCall() const = 0;
        virtual uint64_t nowMs() const = 0;
        virtual void startTimer(Timer timer, unsigned ms) = 0;
        virtual void stopTimer(Timer timer) = 0;
        virtual void log(bool warning, const std::string &message) = 0;
    };

    static constexpr uint64_t kDrainedUsec = 2000;  //!< below this a buffer is empty
    static constexpr unsigned kSamples = 5;         //!< looks at an empty stream
    static constexpr unsigned kSampleGapMs = 500;
    static constexpr unsigned kDeadlineMs = 3000;   //!< sleepd waits 5 s for prepareSuspend
    static constexpr unsigned kActivityMs = 5000;   //!< legacy's
    static constexpr unsigned kHoldLimitMs = 60000;
    static constexpr uint64_t kCallLimitMs = 6ULL * 60 * 60 * 1000;

    //! What the streams of one sample come to.
    struct Count
    {
        unsigned playing = 0;   //!< counted
        unsigned drained = 0;   //!< uncorked, nothing in the buffer
        unsigned brief = 0;     //!< feedback and DTMF
        std::map<std::string, unsigned> sinks;  //!< of playing, per class
    };
    static Count count(const std::vector<Stream> &streams);
    //! A class's name as the end of an activity id: sleepd writes that to
    //! /sys/power/wake_lock, where a space would end the name, and media.type
    //! is whatever a client set.
    static std::string activityId(const std::string &sink);

    explicit SuspendVoter(Host &host) : mHost(host) {}

    //! com.palm.sleep came up or went.
    void sleepdUp();
    void sleepdDown();
    //! The reply to identify(): every later one is its subscription ending.
    void identified(const std::string &payload);

    //! sleepd's signals.
    void suspendRequest() { begin(Round::Request); }
    void prepareSuspend() { begin(Round::Prepare); }
    void resume();

    //! The answers to queryStreams().
    void streams(const std::vector<Stream> &streams);
    void streamsFailed();
    //! PulseAudio came, or went.
    void paReady();
    void paLost();

    void timerFired(Timer timer);

    //! audiod is stopping: PulseAudio stays, sleepd is told by the bus.
    void stop();

    bool registered() const { return !mClientId.empty(); }

private:
    void begin(Round round);
    void sample();
    void answer(bool ack, bool paAnswered);
    void nack(const std::string &sink, const std::string &why);
    void release();
    const char *roundName() const;

    Host &mHost;
    std::string mClientId;
    bool mVoting = false;
    Round mRound = Round::Request;
    unsigned mSamples = 0;
    bool mHeld = false;
    bool mCallSeen = false;
    uint64_t mCallSince = 0;
};

#endif // _SUSPEND_VOTER_H
