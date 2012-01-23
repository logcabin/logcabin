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

#include <sys/time.h>

#include "DLogEvent.h"

/**
 * Event Loop internal definitions.
 */

#ifndef LIBDLOGRPC_DLOGEVENTINT_H
#define LIBDLOGRPC_DLOGEVENTINT_H

namespace DLog {
namespace RPC {

/**
 * EventSocketPriv object that encapsulates the event-loop implementation
 * specific functionality for EventSocket objects.
 */
class EventSocketPriv {
  public:
    EventSocketPriv() { }
    virtual ~EventSocketPriv() { }
    virtual bool bind(int fd) = 0;
    virtual bool connect(const char* ip, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual int write(const void* buf, int length) = 0;
    virtual void setReadWatermark(int length) = 0;
    virtual size_t getLength() = 0;
    virtual int read(void* buf, int length) = 0;
    virtual int discard(int length) = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

/**
 * EventListenerPriv object that encapsulates the event-loop implementation
 * specific functionality for EventListener objects.
 */
class EventListenerPriv {
  public:
    EventListenerPriv() { }
    virtual ~EventListenerPriv() { }
    virtual bool bind(uint16_t port) = 0;
};

/**
 * EventSignalPriv object that encapsulates the event-loop implementation
 * specific functionality for EventSignal objects.
 */
class EventSignalPriv {
  public:
    EventSignalPriv() { }
    virtual ~EventSignalPriv() { };
    virtual void add() = 0;
    virtual void add(time_t seconds) = 0;
    virtual void remove() = 0;
    virtual bool isPending() = 0;
};

/**
 * EventSignalPriv object that encapsulates the event-loop implementation
 * specific functionality for EventSignal objects.
 */
class EventTimerPriv {
  public:
    EventTimerPriv() { }
    virtual ~EventTimerPriv() { };
    virtual void add(time_t seconds) = 0;
    virtual void remove() = 0;
    virtual bool isPending() = 0;
};

} // namespace
} // namespace

#endif // LIBDLOGRPC_DLOGEVENTINT_H

