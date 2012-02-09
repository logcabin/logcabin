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

#ifndef LOGCABIN_EVENT_TIMER_H
#define LOGCABIN_EVENT_TIMER_H

#include "Loop.h"

namespace LogCabin {
namespace Event {

/**
 * A Timer is called by the Event::Loop when time has elapsed.
 * The client should inherit from this and implement the trigger method for
 * when the timer expires.
 *
 * Timers can be added and scheduled from any thread, but they will always fire
 * on the thread running the Event::Loop.
 */
class Timer {
  public:

    /**
     * Construct a timer but do not schedule it to trigger.
     * It will not fire until schedule() is invoked.
     * \param eventLoop
     *      Event::Loop that will manage this timer.
     */
    explicit Timer(Event::Loop& eventLoop);

    /**
     * Destructor.
     */
    virtual ~Timer();

    /**
     * This method is overridden by a subclass and invoked when the timer
     * expires. This method will be invoked by the main event loop on whatever
     * thread is running the Event::Loop.
     */
    virtual void handleTimerEvent() = 0;

    /**
     * Start the timer.
     * \param nanoseconds
     *     The timer will trigger once this number of nanoseconds have elapsed.
     *     If the timer was already scheduled, the old time is forgotten.
     */
    void schedule(uint64_t nanoseconds);

    /**
     * Stop the timer from calling handleTimerEvent().
     * This will cancel the current timer, if any, so that handleTimerEvent()
     * is not called until the next time it is scheduled.
     *
     * This method behaves in a friendly way when called concurrently with the
     * timer firing:
     * - If this method is called from another thread and handleTimerEvent() is
     *   running concurrently on the event loop thread, deschedule() will wait
     *   for handleTimerEvent() to complete before returning.
     * - If this method is called from the event loop thread and
     *   handleTimerEvent() is currently being fired, then deschedule() can't
     *   wait and returns immediately.
     */
    void deschedule();

    /**
     * Returns true if the timer has been scheduled and has not yet fired.
     * \return
     *      True if the timer has been scheduled and will eventually call
     *      handleTimerEvent().
     *      False if the timer is currently running handleTimerEvent() or if it
     *      is not scheduled to call handleTimerEvent() in the future.
     */
    bool isScheduled() const;

    /**
     * Event::Loop that will manage this timer.
     */
    Event::Loop& eventLoop;

  private:
    /**
     * The timer event from libevent.
     * This is never NULL.
     */
    LibEvent::event* event;

    // Timer is not copyable.
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
};

} // namespace LogCabin::Event
} // namespace LogCabin

#endif /* LOGCABIN_EVENT_TIMER_H */
