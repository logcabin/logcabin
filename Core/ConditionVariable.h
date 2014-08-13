/* Copyright (c) 2012-2014 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 5
#include <atomic>
#else
#include <cstdatomic>
#endif
#include <condition_variable>
#include <functional>

#include "Core/Mutex.h"

#ifndef LOGCABIN_CORE_CONDITIONVARIABLE_H
#define LOGCABIN_CORE_CONDITIONVARIABLE_H

namespace LogCabin {
namespace Core {

/**
 * A wrapper around std::condition_variable that is useful for testing
 * purposes and works around a bug in libstdc++.
 *
 * You can set a callback to be called when the condition variable is
 * waited on; instead of waiting, this callback will be called. This callback
 * can, for example, change some shared state so that the calling thread's
 * condition is satisfied. It also counts how many times the condition variable
 * has been notified.
 *
 * The interface to this class is the same as std::condition_variable.
 * This class also goes through some trouble so that it can be used with
 * std::unique_lock<Mutex>, since normal condition variables may only be used
 * with std::unique_lock<std::mutex>.
 */
class ConditionVariable {
  public:
    ConditionVariable()
        : cv()
        , callback()
        , notificationCount(0)
        , lastWaitUntilTimeSinceEpoch()
    {
    }

    void
    notify_one() {
        ++notificationCount;
        cv.notify_one();
    }

    void
    notify_all() {
        ++notificationCount;
        cv.notify_all();
    }

    void
    wait(std::unique_lock<std::mutex>& lockGuard) {
        if (callback) {
            lockGuard.unlock();
            callback();
            lockGuard.lock();
        } else {
            cv.wait(lockGuard);
        }
    }

    void
    wait(std::unique_lock<Core::Mutex>& lockGuard) {
        Core::Mutex& mutex(*lockGuard.mutex());
        if (mutex.callback)
            mutex.callback();
        assert(lockGuard);
        std::unique_lock<std::mutex> stdLockGuard(mutex.m,
                                                  std::adopt_lock_t());
        lockGuard.release();
        wait(stdLockGuard);
        assert(stdLockGuard);
        lockGuard = std::unique_lock<Core::Mutex>(mutex, std::adopt_lock_t());
        stdLockGuard.release();
        if (mutex.callback)
            mutex.callback();
    }


    // wait_for isn't exposed since it doesn't make much sense to me in light
    // of spurious interrupts.

    // Returns void instead of cv_status since it's almost always clearer to
    // check whether timeout has elapsed explicitly. Also, gcc 4.4 doesn't have
    // cv_status and uses bool instead.
    template<typename Clock, typename Duration>
    void
    wait_until(std::unique_lock<std::mutex>& lockGuard,
               const std::chrono::time_point<Clock, Duration>& abs_time) {
        lastWaitUntilTimeSinceEpoch =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                                            abs_time.time_since_epoch());
        if (callback) {
            lockGuard.unlock();
            callback();
            lockGuard.lock();
        } else {
            // Work-around for bug in libstdc++: wait for no more than an hour
            // to avoid overflow. See
            // http://gcc.gnu.org/bugzilla/show_bug.cgi?id=58931
            std::chrono::time_point<Clock, Duration> now = Clock::now();
            if (abs_time < now + std::chrono::hours(1))
                cv.wait_until(lockGuard, abs_time);
            else
                cv.wait_until(lockGuard, now + std::chrono::hours(1));
        }
    }

    template<typename Clock, typename Duration>
    void
    wait_until(std::unique_lock<Core::Mutex>& lockGuard,
               const std::chrono::time_point<Clock, Duration>& abs_time) {
        Core::Mutex& mutex(*lockGuard.mutex());
        if (mutex.callback)
            mutex.callback();
        assert(lockGuard);
        std::unique_lock<std::mutex> stdLockGuard(mutex.m,
                                                  std::adopt_lock_t());
        lockGuard.release();
        wait_until(stdLockGuard, abs_time);
        assert(stdLockGuard);
        lockGuard = std::unique_lock<Core::Mutex>(mutex, std::adopt_lock_t());
        stdLockGuard.release();
        if (mutex.callback)
            mutex.callback();
    }

  private:
    /// Underlying condition variable.
    std::condition_variable cv; // NOLINT
    /**
     * This function will be called with the lock released during every
     * invocation of wait/wait_until. No wait will actually occur; this is only
     * used for unit testing.
     */
    std::function<void()> callback;
  public:
    /**
     * The number of times this condition variable has been notified.
     */
    std::atomic<uint64_t> notificationCount;
    /**
     * In the last call to wait_until, the timeout that the caller provided.
     * This is used in some unit tests to check that timeouts are set
     * correctly. It is stored as the number of milliseconds after the
     * clock's epoch. (Since we don't know the Clock in advance, we can't store
     * a time_point here.)
     */
    std::chrono::milliseconds lastWaitUntilTimeSinceEpoch;
};

} // namespace LogCabin::Core
} // namespace LogCabin

#endif // LOGCABIN_CORE_CONDITIONVARIABLE_H
