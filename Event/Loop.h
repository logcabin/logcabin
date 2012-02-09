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
class Timer;

/**
 * This class contains an event loop based on the libevent2 library.
 * It keeps track of interesting events such as timers and socket activity, and
 * arranges for callbacks to be invoked when the events happen.
 */
class Loop {
  public:
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
     * Run the main event loop until exit() is called or there are no more
     * events that could possibly fire.
     */
    void runForever();

    /**
     * Exit the main event loop, if one is running.
     * This may be called from an event handler or from any thread.
     * It is safe to run again after calling exit.
     */
    void exit();

  private:
    /**
     * The core of the event loop from libevent.
     * This is never NULL.
     */
    LibEvent::event_base* base;

    // Event types are friends, since they need to mess with 'base'.
    friend class Timer;

    // EventLoop is not copyable.
    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;
}; // class Loop

} // namespace LogCabin::Event
} // namespace LogCabin

#endif /* LOGCABIN_EVENT_LOOP_H */
