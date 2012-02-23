/* Copyright (c) 2011-2012 Stanford University
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

#ifndef LOGCABIN_EVENT_LOOP_H
#define LOGCABIN_EVENT_LOOP_H

#include <cinttypes>
#include <condition_variable>
#include <mutex>

/**
 * This is a container for types that are used in place of the unqualified
 * types found in libevent2. The idea is to not pollute the global namespace
 * with libevent2's unqualified types.
 */
namespace LibEvent {
class event_base;
class event;
}

namespace LogCabin {
namespace Event {

// forward declarations
class File;
class Signal;
class Timer;

/**
 * This class contains an event loop based on the libevent2 library.
 * It keeps track of interesting events such as timers and socket activity, and
 * arranges for callbacks to be invoked when the events happen.
 */
class Loop {
  public:

    /**
     * Lock objects are used to synchronize between the Event::Loop thread and
     * other threads.  As long as a Lock object exists the following guarantees
     * are in effect: either (a) the thread is the event loop thread or (b) no
     * other thread has a Lock object and the event loop thread is waiting for
     * the Lock to be destroyed. Locks may be used recursively.
     */
    class Lock {
      public:
        /// Constructor. Acquire the lock.
        explicit Lock(Event::Loop& eventLoop);
        /// Destructor. Release the lock.
        ~Lock();
        /// Event::Loop to lock.
        Event::Loop& eventLoop;
      private:
        // Lock is not copyable.
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    };

    /**
     * Constructor.
     */
    Loop();

    /**
     * Destructor. The caller must ensure that no events still exist and that
     * the event loop is not running.
     */
    ~Loop();

    /**
     * Run the main event loop until exit() is called.
     * It is safe to call this again after it returns.
     * The caller must ensure that only one thread is executing runForever() at
     * a time.
     */
    void runForever();

    /**
     * Exit the main event loop, if one is running. It may return before
     * runForever() has returned but guarantees runForever() will return soon.
     *
     * If the event loop is not running, then the next time it runs, it will
     * exit right away (these semantics can be useful to avoid races).
     *
     * This may be called from an event handler or from any thread.
     */
    void exit();

  private:
    /**
     * The core of the event loop from libevent.
     * This is never NULL.
     */
    LibEvent::event_base* base;

    /**
     * This event is scheduled by Lock and exit() to break out of libevent's
     * main event loop. (It's not an Event::Timer instance because that creates
     * strange dependencies. Specifically, Event::Timer wouldn't be able to use
     * Event::Loop::Lock if it wanted to, and this member's construction would
     * have to be deferred until the end of the constructor.)
     */
    LibEvent::event* exitEvent;

    /**
     * This mutex protects the rest of the members of this class.
     */
    std::mutex mutex;

    /**
     * The thread ID of the thread running the event loop, or
     * Core::ThreadId::NONE if no thread is currently running the event loop.
     * This serves two purposes:
     * First, it allows Lock to tell whether it's running under the event loop.
     * Second, it allows Lock to tell if the event loop is running.
     */
    uint64_t runningThread;

    /**
     * This is a flag to runForever() to exit, set by exit().
     */
    bool shouldExit;

    /**
     * The number of Lock instances, including those that are blocked and those
     * that are active.
     * runForever() waits for this to drop to 0 before running again.
     */
    uint32_t numLocks;

    /**
     * The number of Locks that are active. This is used to support reentrant
     * Lock objects, specifically to know when to set #lockOwner back to
     * Core::ThreadId::NONE.
     */
    uint32_t numActiveLocks;

    /**
     * The thread ID of the thread with active Locks, or Core::ThreadId::NONE
     * if no thread currently has a Lock. This allows for mutually exclusive
     * yet reentrant Lock objects.
     */
    uint64_t lockOwner;

    /**
     * Lock instances wait on this for the event loop to leave libevent's main
     * loop.
     */
    std::condition_variable eventLoopIdle;

    /**
     * runForever() waits on this for Locks to be released.
     */
    std::condition_variable lockGone;

    // Event types are friends, since they need to mess with 'base'.
    friend class File;
    friend class Signal;
    friend class Timer;

    // Loop is not copyable.
    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;
}; // class Loop

} // namespace LogCabin::Event
} // namespace LogCabin

#endif /* LOGCABIN_EVENT_LOOP_H */
