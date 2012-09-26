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

#ifndef LOGCABIN_EVENT_SIGNAL_H
#define LOGCABIN_EVENT_SIGNAL_H

#include "Loop.h"

namespace LogCabin {
namespace Event {

/**
 * A Signal is called by the Event::Loop when a Unix signal is received.
 * The client should inherit from this and implement the handleSignalEvent()
 * method for when the signal is received.
 *
 * Signal handlers can be created from any thread, but they will always fire on
 * the thread running the Event::Loop.
 */
class Signal {
  public:

    /**
     * Construct and enable a signal handler.
     * \param eventLoop
     *      Event::Loop that will manage this signal handler.
     * \param signalNumber
     *      The signal number identifying which signal to receive
     *      (man signal.h).
     */
    explicit Signal(Event::Loop& eventLoop, int signalNumber);

    /**
     * Destructor.
     */
    virtual ~Signal();

    /**
     * This method is overridden by a subclass and invoked when the signal
     * is received. This method will be invoked by the main event loop on
     * whatever thread is running the Event::Loop.
     */
    virtual void handleSignalEvent() = 0;

    /**
     * Event::Loop that will manage this signal handler.
     */
    Event::Loop& eventLoop;

    /**
     * The signal number identifying which signal to receive (man signal.h).
     */
    const int signalNumber;

  private:
    /**
     * The signal event from libevent.
     * This is never NULL.
     */
    LibEvent::event* event;

    // Signal is not copyable.
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
};

} // namespace LogCabin::Event
} // namespace LogCabin

#endif /* LOGCABIN_EVENT_SIGNAL_H */
