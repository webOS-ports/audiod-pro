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

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <pbnjson.hpp>
#include <string>
#include <vector>

#include "suspendVoter.h"

// SUSPEND-VOTE: audio really playing or capturing, or a call, keeps the phone
// up; nothing else does, and sleepd always gets its answer.

namespace {

typedef SuspendVoter::Stream Stream;
typedef SuspendVoter::Timer Timer;

Stream playing(const std::string &sink = "pmedia")
{
    Stream s;
    s.client = true;
    s.bufferUsec = 260000;
    s.sink = sink;
    return s;
}

// Spotify paused: gst-launch stopped, the stream uncorked, nothing in its
// buffer but the resampler's delay (183-201 us read on the mindphone).
Stream stalled(const std::string &sink = "pdefaultapp")
{
    Stream s = playing(sink);
    s.bufferUsec = 190;
    return s;
}

Stream corked(const std::string &sink = "pmedia")
{
    Stream s = playing(sink);
    s.corked = true;
    return s;
}

Stream capturing()
{
    Stream s;
    s.capture = true;
    s.client = true;
    s.sink = "precord";
    return s;
}

// module-remap-sink's stream on the card: uncorked, no client, empty buffer.
Stream remap()
{
    Stream s;
    s.bufferUsec = 0;
    return s;
}

class FakeHost : public SuspendVoter::Host
{
public:
    struct Call
    {
        std::string method;
        std::string payload;
    };

    void identify() override { ++identifies; }
    void call(const char *method, const std::string &payload) override
    {
        calls.push_back({method, payload});
    }
    bool queryStreams() override
    {
        ++queries;
        querying = paThere;
        return paThere;
    }
    void cancelQuery() override { querying = false; }
    void holdAudio(bool hold) override { holds.push_back(hold); }
    bool inCall() const override { return call_; }
    uint64_t nowMs() const override { return now; }
    void startTimer(Timer timer, unsigned ms) override
    {
        timers[static_cast<size_t>(timer)] = ms;
    }
    void stopTimer(Timer timer) override { timers[static_cast<size_t>(timer)].reset(); }
    void log(bool, const std::string &message) override { logged.push_back(message); }

    bool timer(Timer timer) const { return timers[static_cast<size_t>(timer)].has_value(); }
    bool loggedWith(const std::string &part) const
    {
        for (const std::string &line : logged)
        {
            if (line.find(part) != std::string::npos)
                return true;
        }
        return false;
    }
    //! The calls to one method.
    std::vector<pbnjson::JValue> sent(const std::string &method) const
    {
        std::vector<pbnjson::JValue> found;
        for (const Call &c : calls)
        {
            if (c.method == method)
                found.push_back(pbnjson::JDomParser::fromString(c.payload));
        }
        return found;
    }

    int identifies = 0;
    int queries = 0;
    bool querying = false;
    bool paThere = true;
    bool call_ = false;
    uint64_t now = 1000;
    std::vector<Call> calls;
    std::vector<bool> holds;
    std::vector<std::string> logged;
    std::array<std::optional<unsigned>, static_cast<size_t>(Timer::Count)> timers;
};

class SuspendVote : public ::testing::Test
{
protected:
    void SetUp() override
    {
        voter.sleepdUp();
        voter.identified(R"({"subscribed":true,"clientId":"c1","returnValue":true})");
        host.calls.clear();
        host.logged.clear();
    }

    //! The answer sleepd has had to round's question: nullopt for none.
    std::optional<bool> vote(const char *method)
    {
        const auto answers = host.sent(method);
        if (answers.empty())
            return std::nullopt;
        EXPECT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0]["clientId"].asString(), "c1");
        return answers[0]["ack"].asBool();
    }
    std::optional<bool> requestVote() { return vote("suspendRequestAck"); }
    std::optional<bool> prepareVote() { return vote("prepareSuspendAck"); }

    //! Runs sleepd's question with these streams for every look.
    void looksAt(const std::vector<Stream> &streams)
    {
        for (unsigned looks = 0; host.querying; ++looks)
        {
            if (looks > SuspendVoter::kSamples)
            {
                ADD_FAILURE() << "still looking after " << looks << " looks";
                return;
            }
            host.querying = false;
            voter.streams(streams);
            if (host.timer(Timer::NextSample))
            {
                host.timers[static_cast<size_t>(Timer::NextSample)].reset();
                voter.timerFired(Timer::NextSample);
            }
        }
    }

    FakeHost host;
    SuspendVoter voter{host};
};

} // namespace

TEST(SuspendVoteCount, OnlyAClientsUncorkedStreamWithSoundInItCounts)
{
    EXPECT_EQ(SuspendVoter::count({}).playing, 0u);
    EXPECT_EQ(SuspendVoter::count({playing()}).playing, 1u);
    EXPECT_EQ(SuspendVoter::count({corked()}).playing, 0u);
    EXPECT_EQ(SuspendVoter::count({corked()}).drained, 0u);
    EXPECT_EQ(SuspendVoter::count({remap()}).playing, 0u);
    EXPECT_EQ(SuspendVoter::count({remap()}).drained, 0u);
    EXPECT_EQ(SuspendVoter::count({stalled()}).playing, 0u);
    EXPECT_EQ(SuspendVoter::count({stalled()}).drained, 1u);
    EXPECT_EQ(SuspendVoter::count({capturing()}).playing, 1u);
}

TEST(SuspendVoteCount, TheBufferDecidesAtTheLineBetweenEmptyAndFed)
{
    Stream s = playing();
    // pacat at 10 ms against a PulseAudio built like the device's: 7500 us fed.
    s.bufferUsec = 7500;
    EXPECT_EQ(SuspendVoter::count({s}).playing, 1u);
    s.bufferUsec = SuspendVoter::kDrainedUsec;
    EXPECT_EQ(SuspendVoter::count({s}).playing, 1u);
    s.bufferUsec = SuspendVoter::kDrainedUsec - 1;
    EXPECT_EQ(SuspendVoter::count({s}).drained, 1u);
}

TEST(SuspendVoteCount, ACaptureStreamHasNoBufferToJudgeBy)
{
    Stream s = capturing();
    s.bufferUsec = 0;
    EXPECT_EQ(SuspendVoter::count({s}).playing, 1u);
}

TEST(SuspendVoteCount, ACorkedCaptureDoesNotCount)
{
    Stream s = capturing();
    s.corked = true;
    EXPECT_EQ(SuspendVoter::count({s}).playing, 0u);
}

TEST(SuspendVoteCount, FeedbackAndDtmfAreBriefWhateverTheirBuffer)
{
    const auto c = SuspendVoter::count({playing("pfeedback"), playing("pDTMF"), stalled("pDTMF")});
    EXPECT_EQ(c.brief, 3u);
    EXPECT_EQ(c.playing, 0u);
    EXPECT_EQ(c.drained, 0u);
}

TEST(SuspendVoteCount, RingtonesAlertsAndMediaAreNotBrief)
{
    for (const char *sink : {"pringtones", "palerts", "pmedia", "palarm", "pdefaultapp", ""})
        EXPECT_EQ(SuspendVoter::count({playing(sink)}).playing, 1u) << sink;
}

TEST(SuspendVoteCount, TheClassesAreCountedApart)
{
    const auto c = SuspendVoter::count({playing("pmedia"), playing("pmedia"), playing("ptts")});
    EXPECT_EQ(c.playing, 3u);
    EXPECT_EQ(c.sinks.at("pmedia"), 2u);
    EXPECT_EQ(c.sinks.at("ptts"), 1u);
}

TEST(SuspendVoteCount, TheActivityIdIsSafeInAWakeLockName)
{
    EXPECT_EQ(SuspendVoter::activityId("pdefaultapp"), "com.palm.audiod.pdefaultapp");
    EXPECT_EQ(SuspendVoter::activityId("call"), "com.palm.audiod.call");
    EXPECT_EQ(SuspendVoter::activityId("two words"), "com.palm.audiod.stream");
    EXPECT_EQ(SuspendVoter::activityId("a/b\n"), "com.palm.audiod.stream");
    EXPECT_EQ(SuspendVoter::activityId(""), "com.palm.audiod.stream");
    EXPECT_EQ(SuspendVoter::activityId(std::string(33, 'a')), "com.palm.audiod.stream");
}

TEST_F(SuspendVote, RegistersForBothRoundsWhenSleepdIsUp)
{
    FakeHost fresh;
    SuspendVoter v(fresh);
    v.sleepdUp();
    EXPECT_EQ(fresh.identifies, 1);
    EXPECT_FALSE(v.registered());

    v.identified(R"({"subscribed":true,"clientId":"id \"1\"","returnValue":true})");
    EXPECT_TRUE(v.registered());
    for (const char *method : {"suspendRequestRegister", "prepareSuspendRegister"})
    {
        const auto calls = fresh.sent(method);
        ASSERT_EQ(calls.size(), 1u) << method;
        EXPECT_EQ(calls[0]["clientId"].asString(), "id \"1\"");
        EXPECT_TRUE(calls[0]["register"].asBool());
    }
}

TEST_F(SuspendVote, AnIdentifySleepdRefusesLeavesItUnregistered)
{
    voter.identified(R"({"returnValue":false,"errorText":"Denied"})");
    EXPECT_FALSE(voter.registered());
    EXPECT_TRUE(host.loggedWith("did not take"));

    voter.suspendRequest();
    EXPECT_EQ(host.queries, 0);
    EXPECT_FALSE(requestVote());
}

TEST_F(SuspendVote, GarbageFromSleepdIsNotAClientId)
{
    for (const char *reply : {"", "not json", "{}", "[]", R"({"subscribed":true})",
                              R"({"subscribed":true,"clientId":""})",
                              R"({"subscribed":true,"clientId":7})",
                              R"({"subscribed":false,"clientId":"c2"})"})
    {
        voter.identified(reply);
        EXPECT_FALSE(voter.registered()) << reply;
    }
}

TEST_F(SuspendVote, SilenceAcks)
{
    voter.suspendRequest();
    looksAt({});
    EXPECT_EQ(requestVote(), true);
    EXPECT_EQ(host.queries, 1);
    EXPECT_TRUE(host.sent("activityStart").empty());
}

TEST_F(SuspendVote, MusicPlayingNacksAndBooksTheActivityFirst)
{
    voter.suspendRequest();
    looksAt({playing("pmedia")});
    EXPECT_EQ(requestVote(), false);

    ASSERT_GE(host.calls.size(), 2u);
    EXPECT_EQ(host.calls[0].method, "activityStart");
    EXPECT_EQ(host.calls[1].method, "suspendRequestAck");
    const auto activity = host.sent("activityStart");
    EXPECT_EQ(activity[0]["id"].asString(), "com.palm.audiod.pmedia");
    EXPECT_EQ(activity[0]["duration_ms"].asNumber<int32_t>(), 5000);
}

TEST_F(SuspendVote, EveryNackIsLoggedWithTheSinkAndTheCount)
{
    voter.suspendRequest();
    looksAt({playing("pmedia"), playing("pmedia")});
    EXPECT_TRUE(host.loggedWith("NACKing sleepd 'suspend request'"));
    EXPECT_TRUE(host.loggedWith("2 on pmedia"));

    host.logged.clear();
    voter.prepareSuspend();
    looksAt({playing("ptts")});
    EXPECT_TRUE(host.loggedWith("NACKing sleepd 'prepare suspend'"));
    EXPECT_TRUE(host.loggedWith("1 on ptts"));
}

TEST_F(SuspendVote, ThePausedPlayerAcksAtOnce)
{
    voter.suspendRequest();
    looksAt({corked(), remap()});
    EXPECT_EQ(requestVote(), true);
    EXPECT_EQ(host.queries, 1);
}

TEST_F(SuspendVote, ACorkedCallVolumeStreamIsNotPlaying)
{
    Stream voice = corked("pdefaultapp");
    voice.sink.clear();
    voter.suspendRequest();
    looksAt({voice});
    EXPECT_EQ(requestVote(), true);
}

TEST_F(SuspendVote, SpotifyPausedDoesNotHoldThePhoneUp)
{
    voter.suspendRequest();
    looksAt({stalled(), remap()});
    EXPECT_EQ(requestVote(), true);
    EXPECT_EQ(host.queries, static_cast<int>(SuspendVoter::kSamples));
    EXPECT_TRUE(host.sent("activityStart").empty());
    EXPECT_TRUE(host.loggedWith("not counted"));
}

TEST_F(SuspendVote, AnEmptyStreamHasNoAnswerUntilItHasBeenLookedAtAgain)
{
    voter.suspendRequest();
    host.querying = false;
    voter.streams({stalled()});
    EXPECT_EQ(requestVote(), std::nullopt);
    EXPECT_EQ(host.timers[static_cast<size_t>(Timer::NextSample)],
              std::optional<unsigned>(SuspendVoter::kSampleGapMs));
    EXPECT_TRUE(host.timer(Timer::Deadline));
}

TEST_F(SuspendVote, TheLooksFitWellInsideTheDeadlineAndSleepdsWait)
{
    EXPECT_LT((SuspendVoter::kSamples - 1) * SuspendVoter::kSampleGapMs, SuspendVoter::kDeadlineMs);
    EXPECT_LT(SuspendVoter::kDeadlineMs, 5000u); // wait_prepare_suspend_ms
}

TEST_F(SuspendVote, AStreamRebufferingForAMomentNacks)
{
    voter.suspendRequest();
    host.querying = false;
    voter.streams({stalled()});
    voter.timerFired(Timer::NextSample);
    host.querying = false;
    voter.streams({playing("pdefaultapp")});
    EXPECT_EQ(requestVote(), false);
    EXPECT_FALSE(host.timer(Timer::NextSample));
}

TEST_F(SuspendVote, EachVoteLooksAfresh)
{
    voter.suspendRequest();
    looksAt({playing()});
    EXPECT_EQ(requestVote(), false);

    host.calls.clear();
    voter.suspendRequest();
    looksAt({corked()});
    EXPECT_EQ(requestVote(), true);
}

TEST_F(SuspendVote, FeedbackDoesNotHoldThePhoneUp)
{
    voter.suspendRequest();
    looksAt({playing("pfeedback"), playing("pDTMF")});
    EXPECT_EQ(requestVote(), true);
    EXPECT_TRUE(host.loggedWith("very short"));
}

TEST_F(SuspendVote, RecordingNacks)
{
    voter.suspendRequest();
    looksAt({capturing()});
    EXPECT_EQ(requestVote(), false);
    EXPECT_TRUE(host.loggedWith("1 on precord"));
}

TEST_F(SuspendVote, ACallNacksWithoutAskingPulseAudio)
{
    host.call_ = true;
    voter.suspendRequest();
    EXPECT_EQ(requestVote(), false);
    EXPECT_EQ(host.queries, 0);
    const auto activities = host.sent("activityStart");
    ASSERT_EQ(activities.size(), 1u);
    EXPECT_EQ(activities[0]["id"].asString(), "com.palm.audiod.call");
    EXPECT_TRUE(host.loggedWith("a call is up"));
}

TEST_F(SuspendVote, ACallNacksWithPulseAudioGoneToo)
{
    host.call_ = true;
    host.paThere = false;
    voter.suspendRequest();
    EXPECT_EQ(requestVote(), false);
}

TEST_F(SuspendVote, ACallFlagLeftBehindStopsNackingAfterTheLimit)
{
    host.call_ = true;
    voter.suspendRequest();
    EXPECT_EQ(requestVote(), false);

    host.calls.clear();
    host.now += SuspendVoter::kCallLimitMs - 1;
    voter.suspendRequest();
    EXPECT_EQ(requestVote(), false);

    host.calls.clear();
    host.now += 1;
    voter.suspendRequest();
    looksAt({});
    EXPECT_EQ(requestVote(), true);
    EXPECT_TRUE(host.loggedWith("not refusing"));
}

TEST_F(SuspendVote, MusicStillCountsOnceACallFlagHasGoneStale)
{
    host.call_ = true;
    voter.suspendRequest();
    host.calls.clear();
    host.now += SuspendVoter::kCallLimitMs;
    voter.suspendRequest();
    looksAt({playing()});
    EXPECT_EQ(requestVote(), false);
}

TEST_F(SuspendVote, AVoteWithoutACallStartsTheLimitOver)
{
    host.call_ = true;
    voter.suspendRequest();
    host.call_ = false;
    host.now += SuspendVoter::kCallLimitMs;
    voter.suspendRequest();
    looksAt({});

    host.calls.clear();
    host.call_ = true;
    voter.suspendRequest();
    EXPECT_EQ(requestVote(), false);
}

TEST_F(SuspendVote, PulseAudioGoneAcksAtOnce)
{
    host.paThere = false;
    voter.suspendRequest();
    EXPECT_EQ(requestVote(), true);
    EXPECT_FALSE(host.timer(Timer::Deadline));
}

TEST_F(SuspendVote, PulseAudioGoingDuringTheVoteAcks)
{
    voter.suspendRequest();
    voter.paLost();
    EXPECT_EQ(requestVote(), true);
    EXPECT_FALSE(host.querying);
    EXPECT_FALSE(host.timer(Timer::Deadline));
    EXPECT_TRUE(host.loggedWith("PulseAudio went"));
}

TEST_F(SuspendVote, PulseAudioFailingTheListingAcks)
{
    voter.suspendRequest();
    voter.streamsFailed();
    EXPECT_EQ(requestVote(), true);
}

TEST_F(SuspendVote, PulseAudioNotAnsweringAcksAtTheDeadline)
{
    voter.suspendRequest();
    EXPECT_EQ(host.timers[static_cast<size_t>(Timer::Deadline)],
              std::optional<unsigned>(SuspendVoter::kDeadlineMs));
    EXPECT_EQ(requestVote(), std::nullopt);

    voter.timerFired(Timer::Deadline);
    EXPECT_EQ(requestVote(), true);
    EXPECT_FALSE(host.querying);
}

TEST_F(SuspendVote, ALateAnswerFromPulseAudioIsDropped)
{
    voter.suspendRequest();
    voter.timerFired(Timer::Deadline);
    host.calls.clear();
    voter.streams({playing()});
    voter.streamsFailed();
    EXPECT_TRUE(host.calls.empty());
}

TEST_F(SuspendVote, ATimerAfterTheVoteDoesNothing)
{
    voter.suspendRequest();
    looksAt({});
    host.calls.clear();
    const int queries = host.queries;
    voter.timerFired(Timer::Deadline);
    voter.timerFired(Timer::NextSample);
    EXPECT_TRUE(host.calls.empty());
    EXPECT_EQ(host.queries, queries);
}

TEST_F(SuspendVote, TheTimersOfAVoteAreStoppedWithItsAnswer)
{
    voter.suspendRequest();
    host.querying = false;
    voter.streams({stalled()});
    EXPECT_TRUE(host.timer(Timer::NextSample));
    voter.paLost();
    EXPECT_FALSE(host.timer(Timer::NextSample));
    EXPECT_FALSE(host.timer(Timer::Deadline));
}

TEST_F(SuspendVote, AQuestionDuringAnOpenVoteClosesTheOldOne)
{
    voter.suspendRequest();
    voter.prepareSuspend();
    EXPECT_EQ(requestVote(), true);
    EXPECT_EQ(prepareVote(), std::nullopt);
    looksAt({playing()});
    EXPECT_EQ(prepareVote(), false);
}

TEST_F(SuspendVote, NothingIsAnsweredWithoutAClientId)
{
    voter.sleepdDown();
    voter.suspendRequest();
    voter.prepareSuspend();
    EXPECT_EQ(host.queries, 0);
    EXPECT_TRUE(host.calls.empty());
}

TEST_F(SuspendVote, SleepdGoingDropsTheVote)
{
    voter.suspendRequest();
    voter.sleepdDown();
    EXPECT_FALSE(voter.registered());
    EXPECT_FALSE(host.querying);
    EXPECT_FALSE(host.timer(Timer::Deadline));
    EXPECT_EQ(requestVote(), std::nullopt);
}

TEST_F(SuspendVote, SleepdComingBackRegistersAgain)
{
    voter.sleepdDown();
    voter.sleepdUp();
    EXPECT_EQ(host.identifies, 2);
    voter.identified(R"({"subscribed":true,"clientId":"c1","returnValue":true})");
    EXPECT_EQ(host.sent("suspendRequestRegister").size(), 1u);
    EXPECT_EQ(host.sent("prepareSuspendRegister").size(), 1u);

    host.calls.clear();
    voter.suspendRequest();
    looksAt({});
    EXPECT_EQ(requestVote(), true);
}

TEST_F(SuspendVote, SleepdComingUpAgainWithoutGoingForgetsTheOldId)
{
    voter.sleepdUp();
    EXPECT_FALSE(voter.registered());
}

TEST_F(SuspendVote, TheLastAckHoldsTheAudioUntilResume)
{
    voter.prepareSuspend();
    looksAt({});
    EXPECT_EQ(prepareVote(), true);
    EXPECT_EQ(host.holds, std::vector<bool>({true}));
    EXPECT_EQ(host.timers[static_cast<size_t>(Timer::Hold)],
              std::optional<unsigned>(SuspendVoter::kHoldLimitMs));

    voter.resume();
    EXPECT_EQ(host.holds, std::vector<bool>({true, false}));
    EXPECT_FALSE(host.timer(Timer::Hold));
}

TEST_F(SuspendVote, TheHoldGoesBeforeTheAck)
{
    class Order : public FakeHost
    {
    public:
        void holdAudio(bool hold) override
        {
            FakeHost::holdAudio(hold);
            callsAtHold = calls.size();
        }
        size_t callsAtHold = 99;
    } order;
    SuspendVoter v(order);
    v.sleepdUp();
    v.identified(R"({"subscribed":true,"clientId":"c1"})");
    order.calls.clear();
    v.prepareSuspend();
    v.streams({});
    EXPECT_EQ(order.callsAtHold, 0u);
    EXPECT_EQ(order.sent("prepareSuspendAck").size(), 1u);
}

TEST_F(SuspendVote, SpotifyPausedIsHeldToo)
{
    voter.prepareSuspend();
    looksAt({stalled()});
    EXPECT_EQ(prepareVote(), true);
    EXPECT_EQ(host.holds, std::vector<bool>({true}));
}

TEST_F(SuspendVote, NoHoldOnTheFirstRoundOrOnANack)
{
    voter.suspendRequest();
    looksAt({});
    EXPECT_EQ(requestVote(), true);
    voter.prepareSuspend();
    looksAt({playing()});
    EXPECT_EQ(prepareVote(), false);
    EXPECT_TRUE(host.holds.empty());
}

TEST_F(SuspendVote, NoHoldWithoutAnAnswerFromPulseAudio)
{
    host.paThere = false;
    voter.prepareSuspend();
    EXPECT_EQ(prepareVote(), true);
    EXPECT_TRUE(host.holds.empty());

    host.paThere = true;
    host.calls.clear();
    voter.prepareSuspend();
    voter.timerFired(Timer::Deadline);
    EXPECT_EQ(prepareVote(), true);
    EXPECT_TRUE(host.holds.empty());

    host.calls.clear();
    voter.prepareSuspend();
    voter.paLost();
    EXPECT_EQ(prepareVote(), true);
    EXPECT_TRUE(host.holds.empty());
}

TEST_F(SuspendVote, AResumeWithNothingHeldLeavesTheAudioAlone)
{
    voter.resume();
    EXPECT_TRUE(host.holds.empty());
}

TEST_F(SuspendVote, SleepdGoingLetsTheAudioGo)
{
    voter.prepareSuspend();
    looksAt({});
    voter.sleepdDown();
    EXPECT_EQ(host.holds, std::vector<bool>({true, false}));
}

TEST_F(SuspendVote, ResumeNeverComingLetsTheAudioGoAtTheLimit)
{
    voter.prepareSuspend();
    looksAt({});
    voter.timerFired(Timer::Hold);
    EXPECT_EQ(host.holds, std::vector<bool>({true, false}));
    EXPECT_TRUE(host.loggedWith("No resume"));
}

TEST_F(SuspendVote, TheNextAttemptLetsAForgottenHoldGo)
{
    voter.prepareSuspend();
    looksAt({});
    voter.suspendRequest();
    EXPECT_EQ(host.holds, std::vector<bool>({true, false}));
}

TEST_F(SuspendVote, PulseAudioGoingEndsTheHoldWithIt)
{
    voter.prepareSuspend();
    looksAt({});
    voter.paLost();
    EXPECT_EQ(host.holds, std::vector<bool>({true}));
    EXPECT_FALSE(host.timer(Timer::Hold));
    voter.resume();
    EXPECT_EQ(host.holds, std::vector<bool>({true}));
}

TEST_F(SuspendVote, PulseAudioComingAlwaysLetsGoOfWhatAnEarlierAudiodHeld)
{
    voter.paReady();
    EXPECT_EQ(host.holds, std::vector<bool>({false}));
}

TEST_F(SuspendVote, StoppingAnswersAndLetsGo)
{
    voter.suspendRequest();
    voter.stop();
    EXPECT_EQ(requestVote(), true);

    host.calls.clear();
    voter.prepareSuspend();
    looksAt({});
    voter.stop();
    EXPECT_EQ(host.holds, std::vector<bool>({true, false}));
}
