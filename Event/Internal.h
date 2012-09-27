/* Copyright (c) 2012 Stanford University
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
 * This file helps to avoid polluting the global namespace with libevent names.
 *
 * Each of the types we need to expose from libevent has a corresponding type
 * in the LibEvent namespace. The qualify() and unqualify() functions safely
 * cast between the two.
 *
 * \warning
 *      This file should only be included from Event implementation files --
 *      never from other header files.
 */

#ifndef LOGCABIN_EVENT_INTERNAL_H
#define LOGCABIN_EVENT_INTERNAL_H

// forward declarations

namespace LibEvent {
struct event_base;
struct event;
struct evconnlistener;
}

struct event_base;
struct event;
struct evconnlistener;

namespace LibEvent {

/**
 * A bit-wise OR of events such as EV_READ, EV_WRITE.
 */
// cpplint complains about the use of short, but it wasn't our choice here.
typedef short EventMask; // NOLINT

/**
 * This allows other files to make sure the libevent library has been
 * initialized. The concern is that other files may depend on this happening,
 * but their code may be copied into some other project that doesn't initialize
 * libevent properly. This boolean allows those files to create a build-time
 * dependency on this file and Internal.cc. It will always have the value
 * true.
 */
extern const bool initialized;

} // namespace LibEvent

/**
 * Converts from fake LibEvent type to actual libevent type.
 * Used to avoid polluting the global namespace.
 */
inline event_base*
unqualify(LibEvent::event_base* base)
{
    return reinterpret_cast<event_base*>(base);
}

/// \copydoc unqualify()
inline event*
unqualify(LibEvent::event* event)
{
    return reinterpret_cast<struct event*>(event);
}

/// \copydoc unqualify()
inline evconnlistener*
unqualify(LibEvent::evconnlistener* listener)
{
    return reinterpret_cast<struct evconnlistener*>(listener);
}

/**
 * Converts from actual libevent type to fake LibEvent type.
 * Used to avoid polluting the global namespace.
 */
inline LibEvent::event_base*
qualify(event_base* base)
{
    return reinterpret_cast<LibEvent::event_base*>(base);
}

/// \copydoc qualify()
inline LibEvent::event*
qualify(event* event)
{
    return reinterpret_cast<LibEvent::event*>(event);
}

/// \copydoc qualify()
inline LibEvent::evconnlistener*
qualify(evconnlistener* listener)
{
    return reinterpret_cast<LibEvent::evconnlistener*>(listener);
}

#endif /* LOGCABIN_EVENT_INTERNAL_H */
