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

#include "DLogEventInt.h"

// libevent2 includes
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>

/**
 * libevent 2.0 Implementation Event Loop definitions.
 */

#ifndef LIBDLOGRPC_DLOGEVENTLE2_H
#define LIBDLOGRPC_DLOGEVENTLE2_H

namespace DLog {
namespace RPC {

// Forward declaration
class EventLoopLE2Impl;

/**
 * libevent2 implementation of the EventSocketPriv object.
 */
class EventSocketLE2Priv : public EventSocketPriv {
  public:
    /**
     * Constructor.
     * \param loop EventLoop to bind to.
     * \param s EventSocket parent object.
     */
    EventSocketLE2Priv(EventLoop& loop, EventSocket& s);
    virtual ~EventSocketLE2Priv();
    /**
     * \copydoc EventSocket::bind
     */
    virtual bool bind(int fd);
    /**
     * \copydoc EventSocket::connect
     */
    virtual bool connect(const char* ip, uint16_t port);
    /**
     * \copydoc EventSocket::write
     */
    virtual int write(const void* buf, int length);
    /**
     * \copydoc EventSocket::setReadWatermark
     */
    virtual void setReadWatermark(int length);
    /**
     * \copydoc EventSocket::getLength
     */
    virtual size_t getLength();
    /**
     * \copydoc EventSocket::read
     */
    virtual int read(void* buf, int length);
    /**
     * \copydoc EventSocket::discard
     */
    virtual int discard(int length);
  private:
    /// libevent bufferevent
    struct bufferevent *bev;
    /// Pointer to parent object
    EventSocket* es;
    /// Pointer to event loop
    EventLoop* loop;
    EventSocketLE2Priv(const EventSocketLE2Priv&) = delete;
    EventSocketLE2Priv& operator=(const EventSocketLE2Priv&) = delete;
};

/**
 * libevent2 implementation of the EventListenerPriv object.
 */
class EventListenerLE2Priv : public EventListenerPriv {
  public:
    /**
     * Constructor.
     * \param loop EventLoop to bind to.
     * \param l EventListener parent object.
     */
    EventListenerLE2Priv(EventLoop& loop, EventListener& l);
    virtual ~EventListenerLE2Priv();
    /**
     * \copydoc EventListener::bind
     */
    virtual bool bind(uint16_t port);
  private:
    /// libevent evconnlistener
    struct evconnlistener* listener;
    /// Pointer to parent object
    EventListener* el;
    /// Pointer to event loop
    EventLoop* loop;
    EventListenerLE2Priv(const EventListenerLE2Priv&) = delete;
    EventListenerLE2Priv& operator=(const EventListenerLE2Priv&) = delete;
};

/**
 * libevent2 implementation of the EventSignalPriv object.
 */
class EventSignalLE2Priv : public EventSignalPriv {
  public:
    EventSignalLE2Priv(EventLoop& loop, EventSignal& s);
    virtual ~EventSignalLE2Priv();
    virtual void add();
    virtual void add(time_t seconds);
    virtual void remove();
    virtual bool isPending();
  private:
    /// libevent event structure
    struct event ev;
    /// Pointer to parent object
    EventSignal* es;
    EventSignalLE2Priv(const EventSignalLE2Priv&) = delete;
    EventSignalLE2Priv& operator=(const EventSignalLE2Priv&) = delete;
};

/**
 * libevent2 implementation of the EventTimerPriv object.
 */
class EventTimerLE2Priv : public EventTimerPriv {
  public:
    EventTimerLE2Priv(EventLoop& loop, EventTimer &t);
    virtual ~EventTimerLE2Priv();
    virtual void add(time_t seconds);
    virtual void remove();
    virtual bool isPending();
  private:
    /// libevent event structure
    struct event ev;
    /// Pointer to parent object
    EventTimer* et;
    EventTimerLE2Priv(const EventTimerLE2Priv&) = delete;
    EventTimerLE2Priv& operator=(const EventTimerLE2Priv&) = delete;
};

/**
 * libevent2 implementation of the EventLoop class.
 */
class EventLoopLE2Impl : public EventLoop {
  public:
    EventLoopLE2Impl();
    virtual ~EventLoopLE2Impl();
    virtual void processEvents();
    virtual void loopBreak();
    virtual void loopExit(time_t seconds);
  private:
    /// event loop base structure
    struct event_base *base;
    EventLoopLE2Impl(const EventLoopLE2Impl&) = delete;
    EventLoopLE2Impl& operator=(const EventLoopLE2Impl&) = delete;
    friend class EventSocketLE2Priv;
    friend class EventListenerLE2Priv;
    friend class EventSignalLE2Priv;
    friend class EventTimerLE2Priv;
};

} // namespace
} // namespace

#endif // LIBDLOGRPC_DLOGEVENTLE2_H

