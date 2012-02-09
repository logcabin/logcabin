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

#include "include/Debug.h"
#include "Internal.h"
#include "Loop.h"
#include "Timer.h"

#include <event2/event.h>

namespace LogCabin {
namespace Event {

namespace {

/**
 * This is called by libevent when any timer fires.
 * \param fd
 *      Unused.
 * \param events
 *      Unused.
 * \param timer
 *      The Event::Timer object whose timer fired.
 */
void
onTimerFired(evutil_socket_t fd, short events, void* timer) // NOLINT
{
    static_cast<Timer*>(timer)->handleTimerEvent();
}

} // anonymous namespace

Timer::Timer(Event::Loop& eventLoop)
    : eventLoop(eventLoop)
    , event(NULL)
{
    event = qualify(evtimer_new(unqualify(eventLoop.base),
                                onTimerFired, this));
    if (event == NULL) {
        PANIC("evtimer_new failed: "
              "No information is available from libevent about this error.");
    }
}

Timer::~Timer()
{
    event_free(unqualify(event));
}

void
Timer::schedule(uint64_t nanoseconds)
{
    const uint64_t nanosPerSecond = 1000 * 1000 * 1000;
    struct timeval timeout;
    timeout.tv_sec  =  nanoseconds / nanosPerSecond;
    timeout.tv_usec = (nanoseconds % nanosPerSecond) / 1000;
    int r = evtimer_add(unqualify(event), &timeout);
    if (r == -1) {
        PANIC("evtimer_add failed: "
              "No information is available from libevent about this error.");
    }
}

void
Timer::deschedule()
{
    int r = evtimer_del(unqualify(event));
    if (r == -1) {
        PANIC("evtimer_add failed: "
              "No information is available from libevent about this error.");
    }
}

bool
Timer::isScheduled() const
{
    return evtimer_pending(unqualify(event), NULL);
}

} // namespace LogCabin::Event
} // namespace LogCabin
