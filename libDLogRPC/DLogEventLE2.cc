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

#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "Debug.h"
#include "DLogEvent.h"
#include "DLogEventInt.h"
#include "DLogEventLE2.h"

/**
 * libevent 2.0 Event Loop Implementation.
 */

namespace DLog {
namespace RPC {

/********************************************************************
 *
 *
 * class EventSocketLE2
 *
 *
 ********************************************************************/

static void
EventSocketLE2ReadCB(struct bufferevent *bev, void *arg)
{
    EventSocket *es = reinterpret_cast<EventSocket *>(arg);

    es->readCB();
}

static void
EventSocketLE2WriteCB(struct bufferevent *bev, void *arg)
{
    EventSocket *es = reinterpret_cast<EventSocket *>(arg);

    es->writeCB();
}

static void
EventSocketLE2EventCB(struct bufferevent *bev, int16_t events, void *arg)
{
    EventSocket::EventMask eventMask = EventSocket::Null;
    int eventErrno = 0;
    EventSocket *es = reinterpret_cast<EventSocket *>(arg);

    // TODO(ali): assert that these are the only event flags received.

    if (events & BEV_EVENT_CONNECTED)
        eventMask = EventSocket::EventMask(eventMask | EventSocket::Connected);
    if (events & BEV_EVENT_EOF)
        eventMask = EventSocket::EventMask(eventMask | EventSocket::Eof);
    if (events & BEV_EVENT_ERROR) {
        eventMask = EventSocket::EventMask(eventMask | EventSocket::Error);
        eventErrno = errno;
    }

    es->eventCB(eventMask, eventErrno);
}

EventSocketLE2Priv::EventSocketLE2Priv(EventLoop& loop, EventSocket& s)
    : bev(),
      es(&s),
      loop(&loop)
{
}

EventSocketLE2Priv::~EventSocketLE2Priv()
{
}

bool
EventSocketLE2Priv::bind(int fd)
{
    EventLoopLE2Impl* impl = static_cast<EventLoopLE2Impl*>(loop);

    bev = bufferevent_socket_new(impl->base, fd,
                                 BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
    if (!bev) {
        return false;
    }

    bufferevent_setcb(bev,
                      EventSocketLE2ReadCB,
                      EventSocketLE2WriteCB,
                      EventSocketLE2EventCB,
                      es);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    return true;
}

bool
EventSocketLE2Priv::connect(const char* ip, uint16_t port)
{
    EventLoopLE2Impl* impl = static_cast<EventLoopLE2Impl*>(loop);
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &sin.sin_addr);
    sin.sin_port = htons(port);

    bev = bufferevent_socket_new(impl->base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        return false;
    }

    bufferevent_setcb(bev,
                      EventSocketLE2ReadCB,
                      EventSocketLE2WriteCB,
                      EventSocketLE2EventCB,
                      this);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    if (bufferevent_socket_connect(bev,
                                   (struct sockaddr*)&sin,
                                   sizeof(sin)) < 0) {
        bufferevent_free(bev);
        bev = NULL;
        return false;
    }

    return true;
}

int
EventSocketLE2Priv::write(const void* buf, int length)
{
    return bufferevent_write(bev, buf, length);
}

void
EventSocketLE2Priv::setReadWatermark(int length)
{
    bufferevent_setwatermark(bev, EV_READ, length, 0);
}

size_t
EventSocketLE2Priv::getLength()
{
    struct evbuffer *input = bufferevent_get_input(bev);

    return evbuffer_get_length(input);
}

int
EventSocketLE2Priv::read(void* buf, int length)
{
    struct evbuffer *input = bufferevent_get_input(bev);

    return evbuffer_remove(input, buf, length);
}

int
EventSocketLE2Priv::discard(int length)
{
    struct evbuffer *input = bufferevent_get_input(bev);

    return evbuffer_drain(input, length);
}

void
EventSocketLE2Priv::lock()
{
    bufferevent_lock(bev);
}

void
EventSocketLE2Priv::unlock()
{
    bufferevent_unlock(bev);
}

/********************************************************************
 *
 *
 * class EventListenerLE2
 *
 *
 ********************************************************************/

static void
EventListenerLE2AcceptCB(struct evconnlistener *listener, evutil_socket_t fd,
                         struct sockaddr *a, int slen, void *arg)
{
    EventListener *el = reinterpret_cast<EventListener *>(arg);

    el->acceptCB(fd);
}

static void
EventListenerLE2ErrorCB(struct evconnlistener *lis, void *arg)
{
    EventListener *el = reinterpret_cast<EventListener *>(arg);

    el->errorCB();
}

EventListenerLE2Priv::EventListenerLE2Priv(EventLoop& loop, EventListener& l)
    : listener(),
      el(&l),
      loop(&loop)
{
}

EventListenerLE2Priv::~EventListenerLE2Priv()
{
    if (listener) {
        evconnlistener_free(listener);
        listener = NULL;
    }
}

bool
EventListenerLE2Priv::bind(uint16_t port)
{
    struct sockaddr_in sin;
    EventLoopLE2Impl* impl = static_cast<EventLoopLE2Impl*>(loop);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);

    listener = evconnlistener_new_bind(impl->base,
                        EventListenerLE2AcceptCB,
                        el,
                        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
                        -1,
                        (struct sockaddr *)&sin, sizeof(sin));
    if (!listener) {
        return false;
    }

    evconnlistener_set_error_cb(listener, EventListenerLE2ErrorCB);

    return true;
}

/********************************************************************
 *
 *
 * class EventSignalLE2
 *
 *
 ********************************************************************/

/**
 * EventSignal libevent2 C callback function. 
 */
static void
EventSignalLE2CB(evutil_socket_t fd, int16_t event, void *arg)
{
    EventSignal *es = reinterpret_cast<EventSignal *>(arg);

    es->trigger();
}

/**
 * Constructor for the libevent EventSignal private object.
 * \param loop EventLoop to bind to.
 * \param s EventSignal parent object.
 */
EventSignalLE2Priv::EventSignalLE2Priv(EventLoop& loop, EventSignal &s)
    : ev(),
      es(&s)
{
    EventLoopLE2Impl* impl = static_cast<EventLoopLE2Impl*>(&loop);
    evsignal_assign(&ev,
                    impl->base,
                    s.getSignal(),
                    EventSignalLE2CB,
                    reinterpret_cast<void *>(&s));
}

EventSignalLE2Priv::~EventSignalLE2Priv()
{
    if (isPending())
        remove();
}

/**
 * \copydoc EventSignal::add
 */
void
EventSignalLE2Priv::add()
{
    evsignal_add(&ev, NULL);
}

/**
 * \copydoc EventSignal::add
 */
void
EventSignalLE2Priv::add(time_t seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    evsignal_add(&ev, &tv);
}

/**
 * \copydoc EventSignal::remove
 */
void
EventSignalLE2Priv::remove()
{
    evsignal_del(&ev);
}

/**
 * \copydoc EventSignal::isPending
 */
bool
EventSignalLE2Priv::isPending()
{
    return evsignal_pending(&ev, NULL);
}

/**
 * EventTimer libevent2 C callback function. 
 */
static void
EventTimerLE2CB(evutil_socket_t fd, int16_t event, void *arg)
{
    EventTimer *et = reinterpret_cast<EventTimer *>(arg);

    et->trigger();
    if (et->isPersistent()) {
        et->addPeriodic(et->getPeriod());
    }
}

/********************************************************************
 *
 *
 * class EventTimerLE2
 *
 *
 ********************************************************************/

/**
 * Constructor for the libevent EventTimer private object.
 * \param loop EventLoop to bind to.
 * \param t EventTimer parent object.
 */
EventTimerLE2Priv::EventTimerLE2Priv(EventLoop& loop, EventTimer &t)
    : ev(),
      et(&t)
{
    EventLoopLE2Impl* impl = static_cast<EventLoopLE2Impl*>(&loop);
    evtimer_assign(&ev,
                   impl->base,
                   EventTimerLE2CB,
                   reinterpret_cast<void *>(&t));
}

EventTimerLE2Priv::~EventTimerLE2Priv()
{
    if (isPending())
        remove();
}

/**
 * \copydoc EventTimer::add
 */
void
EventTimerLE2Priv::add(time_t seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    evtimer_add(&ev, &tv);
}

/**
 * \copydoc EventTimer::remove
 */
void
EventTimerLE2Priv::remove()
{
    evtimer_del(&ev);
}

/**
 * \copydoc EventTimer::isPending
 */
bool
EventTimerLE2Priv::isPending()
{
    return evtimer_pending(&ev, NULL);
}

/**
 * Constructor for the libevent2 implementation of the EventLoop class.
 */
EventLoopLE2Impl::EventLoopLE2Impl() : base(NULL)
{
    int r = evthread_use_pthreads();
    assert(r == 0);
    base = event_base_new();
    assert(base != NULL);
}

EventLoopLE2Impl::~EventLoopLE2Impl()
{
    event_base_free(base);
}

/**
 * Enter the main loop and does not return until all events are processed
 * or loopBreak or loopExit is called.
 */
void
EventLoopLE2Impl::processEvents()
{
    event_base_dispatch(base);
}

/**
 * Forces the loop to exit on the next iteration.
 */
void
EventLoopLE2Impl::loopBreak()
{
    event_base_loopbreak(base);
}

/**
 * Forces the loop to exit after a specified amount of time.
 */
void
EventLoopLE2Impl::loopExit(time_t seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    event_base_loopexit(base, &tv);
}

} // namespace
} // namespace

