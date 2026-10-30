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

#ifndef _SHUTDOWN_GUARD_H
#define _SHUTDOWN_GUARD_H

#include <atomic>
#include <chrono>
#include <functional>
#include <semaphore.h>
#include <thread>

/*
 * Stopping audiod on SIGTERM or SIGINT, within a deadline.
 *
 * The signal handler used to call g_main_loop_quit itself. That takes the
 * main context's lock, which is not safe in a signal handler: when the signal
 * landed on the main thread while it held that lock - likely while the loop
 * spun on the socket of a PulseAudio that had died - the handler waited on
 * the lock forever, and systemd killed audiod after its stop timeout.
 *
 * Here the handler only posts a semaphore. One thread then quits the main
 * loop; another gives the shutdown until the deadline and, if audiod is still
 * there, ends it. The deadline does not depend on the main loop, so a
 * shutdown stuck anywhere - in the loop, a module's teardown or an exit
 * handler - still ends on time.
 */
class ShutdownGuard
{
public:
    using Action = std::function<void()>;

    //! quit stops the main loop; expire runs once the deadline has passed
    //! without finished(), and is expected to end the process.
    ShutdownGuard(Action quit, Action expire, std::chrono::milliseconds deadline);
    ~ShutdownGuard();
    ShutdownGuard(const ShutdownGuard &) = delete;
    ShutdownGuard &operator=(const ShutdownGuard &) = delete;

    //! Starts the shutdown. Safe to call from a signal handler.
    void request() noexcept;
    //! The shutdown is done: the deadline no longer applies.
    void finished() noexcept;
    //! Makes SIGTERM and SIGINT call request(). One guard at a time.
    bool catchSignals();

private:
    void quitWhenRequested();
    void expireWhenLate();

    sem_t mQuitRequested;
    sem_t mDeadlineRequested;
    sem_t mFinished;
    std::atomic<bool> mStopping;
    bool mCatching;
    Action mQuit;
    Action mExpire;
    std::chrono::milliseconds mDeadline;
    std::thread mQuitThread;
    std::thread mDeadlineThread;
};

#endif // _SHUTDOWN_GUARD_H
