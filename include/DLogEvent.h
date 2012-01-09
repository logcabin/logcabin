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

/**
 * \file
 * This file declares the interface for DLog's Event interface.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Ref.h"

#ifndef DLOGEVENT_H
#define DLOGEVENT_H

namespace DLog {
namespace RPC {

// Forward declarations
class EventLoop;
class EventSignalPriv;
class EventTimerPriv;

/**
 * A signal event object. The client should inherit from this and implement
 * the trigger method for when the signal is caught.
 */
class EventSignal {
  public:
    EventSignal(EventLoop &loop, int s);
    virtual ~EventSignal();
    void add();
    void add(time_t timeout);
    void remove();
    bool isPending();
    int getSignal();
    /**
     * The signal handler.
     */
    virtual void trigger() = 0;
  private:
    int signal;
    Ref<EventSignalPriv> priv;
};

/**
 * A timer event object. The client should inherit from this and implement
 * the trigger method for when the timer expires.
 */
class EventTimer {
  public:
    explicit EventTimer(EventLoop &loop);
    virtual ~EventTimer();
    void addPeriodic(time_t seconds);
    void add(time_t seconds);
    void remove();
    bool isPending();
    bool isPersistent();
    time_t getPeriod();
    /**
     * The timer callback.
     */
    virtual void trigger() = 0;
  private:
    /// Used to store the period.
    time_t period;
    /// Set to true for periodic timers.
    bool persist;
    Ref<EventTimerPriv> priv;
};

/**
 * Main event loop class.
 */
class EventLoop {
  public:
    /**
     * Constructor for the event loop implementation.
     *
     * TODO(ali): Support multiple event loop implementations.
     */
    static EventLoop* makeEventLoop();
    virtual ~EventLoop() = 0;
    virtual void processEvents() = 0;
    virtual void loopBreak() = 0;
    virtual void loopExit(time_t seconds) = 0;
  protected:
    EventLoop() { };
  private:
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    friend class EventTimerLE2Priv;
};

} // namespace
} // namespace

#endif /* DLOGEVENT_H */
