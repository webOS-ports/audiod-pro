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

#include "shutdownGuard.h"

#include <cerrno>
#include <csignal>
#include <ctime>
#include <utility>

namespace
{
    //! The guard the signal handler posts to; a lock-free atomic, so the
    //! handler may read it.
    std::atomic<ShutdownGuard *> sCatching{nullptr};

    void onSignal(int)
    {
        const int savedErrno = errno;
        ShutdownGuard *guard = sCatching.load();
        if (guard)
            guard->request();
        errno = savedErrno;
    }

    void waitFor(sem_t *sem)
    {
        while (sem_wait(sem) != 0 && errno == EINTR)
            ;
    }
}

ShutdownGuard::ShutdownGuard(Action quit, Action expire, std::chrono::milliseconds deadline) :
    mStopping(false), mCatching(false), mQuit(std::move(quit)), mExpire(std::move(expire)),
    mDeadline(deadline)
{
    sem_init(&mQuitRequested, 0, 0);
    sem_init(&mDeadlineRequested, 0, 0);
    sem_init(&mFinished, 0, 0);
    mQuitThread = std::thread(&ShutdownGuard::quitWhenRequested, this);
    mDeadlineThread = std::thread(&ShutdownGuard::expireWhenLate, this);
}

ShutdownGuard::~ShutdownGuard()
{
    if (mCatching)
    {
        std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGINT, SIG_DFL);
        sCatching.store(nullptr);
    }

    mStopping.store(true);
    sem_post(&mQuitRequested);
    sem_post(&mDeadlineRequested);
    sem_post(&mFinished);
    mQuitThread.join();
    mDeadlineThread.join();

    sem_destroy(&mQuitRequested);
    sem_destroy(&mDeadlineRequested);
    sem_destroy(&mFinished);
}

void ShutdownGuard::request() noexcept
{
    // sem_post is async-signal-safe; the threads do the rest.
    sem_post(&mQuitRequested);
    sem_post(&mDeadlineRequested);
}

void ShutdownGuard::finished() noexcept
{
    sem_post(&mFinished);
}

bool ShutdownGuard::catchSignals()
{
    ShutdownGuard *none = nullptr;
    if (!sCatching.compare_exchange_strong(none, this))
        return false;

    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &action, nullptr) != 0 || sigaction(SIGINT, &action, nullptr) != 0)
    {
        std::signal(SIGTERM, SIG_DFL);
        sCatching.store(nullptr);
        return false;
    }
    mCatching = true;
    return true;
}

void ShutdownGuard::quitWhenRequested()
{
    waitFor(&mQuitRequested);
    if (!mStopping.load())
        mQuit();
}

void ShutdownGuard::expireWhenLate()
{
    waitFor(&mDeadlineRequested);
    if (mStopping.load())
        return;

    // Timed from the request, apart from the quit: a quit that blocks does
    // not hold the deadline back.
    struct timespec at;
    clock_gettime(CLOCK_MONOTONIC, &at);
    const auto ms = mDeadline.count();
    at.tv_sec += static_cast<time_t>(ms / 1000);
    at.tv_nsec += static_cast<long>(ms % 1000) * 1000000L;
    if (at.tv_nsec >= 1000000000L)
    {
        at.tv_sec += 1;
        at.tv_nsec -= 1000000000L;
    }

    int ret;
    while ((ret = sem_clockwait(&mFinished, CLOCK_MONOTONIC, &at)) != 0 && errno == EINTR)
        ;
    if (ret != 0 && !mStopping.load())
        mExpire();
}
