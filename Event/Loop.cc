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

#include <assert.h>
#include <event2/event.h>
#include <event2/thread.h>

#include "include/Debug.h"
#include "Core/ThreadId.h"
#include "Event/Internal.h"
#include "Event/Loop.h"

namespace LogCabin {
namespace Event {

namespace {

/// Libevent callback to break out of the event loop.
void
exitEventLoop(evutil_socket_t fd, short events, void* base) // NOLINT
{
    int r = event_base_loopbreak(static_cast<event_base*>(base));
    if (r == -1) {
        PANIC("event_loop_break failed: No information is available from "
              "libevent about this error.");
    }
}

/// Schedule exitEventLoop() to be called next time libevent gets around to it.
void
scheduleExit(LibEvent::event* exitEvent)
{
    struct timeval timeout = {0, 0};
    int r = evtimer_add(unqualify(exitEvent), &timeout);
    if (r == -1) {
        PANIC("evtimer_add failed: "
              "No information is available from libevent about this error.");
    }
}

} // anonymous namespace

////////// Loop::Lock //////////

Loop::Lock::Lock(Event::Loop& eventLoop)
    : eventLoop(eventLoop)
{
    std::unique_lock<std::mutex> lockGuard(eventLoop.mutex);
    ++eventLoop.numLocks;
    if (eventLoop.runningThread != Core::ThreadId::get() &&
        eventLoop.lockOwner != Core::ThreadId::get()) {
        // This is an actual lock: we're not running inside the event loop, and
        //                         we're not recursively locking.
        // Wait for runForever() to stop running
        while (eventLoop.runningThread != Core::ThreadId::NONE) {
            scheduleExit(eventLoop.exitEvent);
            eventLoop.eventLoopIdle.wait(lockGuard);
        }
        // Wait for other Locks to unlock
        while (eventLoop.lockOwner != Core::ThreadId::NONE)
            eventLoop.lockGone.wait(lockGuard);
        // runForever couldn't have jumped into libevent again, since numLocks
        // has remained positive.
        assert(eventLoop.runningThread == Core::ThreadId::NONE);
        // Take ownership of the lock
        eventLoop.lockOwner = Core::ThreadId::get();
    }
    ++eventLoop.numActiveLocks;
}

Loop::Lock::~Lock()
{
    std::unique_lock<std::mutex> lockGuard(eventLoop.mutex);
    --eventLoop.numLocks;
    --eventLoop.numActiveLocks;
    if (eventLoop.numActiveLocks == 0) {
        eventLoop.lockOwner = Core::ThreadId::NONE;
        eventLoop.lockGone.notify_all();
    }
}

////////// Loop //////////

Loop::Loop()
    : base(NULL)
    , exitEvent(NULL)
    , mutex()
    , runningThread(Core::ThreadId::NONE)
    , shouldExit(false)
    , numLocks(0)
    , numActiveLocks(0)
    , lockOwner(Core::ThreadId::NONE)
    , eventLoopIdle()
    , lockGone()
{
    assert(LibEvent::initialized);
    base = qualify(event_base_new());
    if (base == NULL) {
        PANIC("event_base_new failed: "
              "No information is available from libevent about this error.");
    }

    // Set the number of priority levels to 2.
    // Then by default, events will get priority 2/2=1.
    // Smaller priority numbers will run first.
    // We'll use priority 0 for exitEvent only, which should run quickly.
    int r = event_base_priority_init(unqualify(base), 2);
    if (r != 0) {
        PANIC("event_base_priority_init failed: "
              "No information is available from libevent about this error.");
    }

    exitEvent = qualify(evtimer_new(unqualify(base),
                                    exitEventLoop, unqualify(base)));
    if (exitEvent == NULL) {
        PANIC("evtimer_new failed: "
              "No information is available from libevent about this error.");
    }

    r = event_priority_set(unqualify(exitEvent), 0);
    if (r != 0) {
        PANIC("event_priority_set failed: "
              "No information is available from libevent about this error.");
    }
}

Loop::~Loop()
{
    event_free(unqualify(exitEvent));
    event_base_free(unqualify(base));
}

void
Loop::runForever()
{
    while (true) {
        {
            std::unique_lock<std::mutex> lockGuard(mutex);
            runningThread = Core::ThreadId::NONE;
            // Wait for all Locks to finish up
            while (numLocks > 0) {
                eventLoopIdle.notify_all();
                lockGone.wait(lockGuard);
            }
            if (shouldExit) {
                shouldExit = false;
                return;
            }
            runningThread = Core::ThreadId::get();
        }
        int r = event_base_dispatch(unqualify(base));
        if (r == -1) {
            PANIC("event_loop_dispatch failed: No information is "
                  "available from libevent about this error.");
        }
    }
}

void
Loop::exit()
{
    {
        // Set the flag for runForever to exit.
        std::unique_lock<std::mutex> lockGuard(mutex);
        shouldExit = true;
    }

    // Convince run() to break out of libevent.
    scheduleExit(this->exitEvent);
}

} // namespace LogCabin::Event
} // namespace LogCabin
