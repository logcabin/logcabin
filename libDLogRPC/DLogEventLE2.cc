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

#include "Debug.h"
#include "DLogEvent.h"
#include "DLogEventInt.h"
#include "DLogEventLE2.h"

/**
 * libevent 2.0 Event Loop Implementation.
 */

namespace DLog {
namespace RPC {

static void
EventListenerLE2AcceptCB(struct evconnlistener *listener, evutil_socket_t fd,
                         struct sockaddr *a, int slen, void *arg)
{
    EventListener *el = reinterpret_cast<EventListener *>(arg);

    el->accept(fd);
}

static void
EventListenerLE2ErrorCB(struct evconnlistener *lis, void *arg)
{
    EventListener *el = reinterpret_cast<EventListener *>(arg);

    el->error();
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

