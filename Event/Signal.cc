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

#include "Core/Debug.h"
#include "Internal.h"
#include "Loop.h"
#include "Signal.h"

#include <event2/event.h>

namespace LogCabin {
namespace Event {

namespace {

/**
 * This is called by libevent when any signal fires.
 * \param fd
 *      Unused.
 * \param events
 *      Unused.
 * \param signal
 *      The Event::Signal object whose signal fired.
 */
void
onSignalFired(evutil_socket_t fd, short events, void* signal) // NOLINT
{
    static_cast<Signal*>(signal)->handleSignalEvent();
}

} // anonymous namespace

Signal::Signal(Event::Loop& eventLoop, int signalNumber)
    : eventLoop(eventLoop)
    , event(NULL)
{
    event = qualify(evsignal_new(unqualify(eventLoop.base),
                                 signalNumber,
                                 onSignalFired, this));
    if (event == NULL) {
        PANIC("evsignal_new failed: "
              "No information is available from libevent about this error.");
    }
    int r = evsignal_add(unqualify(event), NULL);
    if (r == -1) {
        PANIC("evsignal_add failed: "
              "No information is available from libevent about this error.");
    }
}

Signal::~Signal()
{
    event_free(unqualify(event));
}

} // namespace LogCabin::Event
} // namespace LogCabin
