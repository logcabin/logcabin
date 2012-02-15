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
class event_base;
}

class event_base;

/**
 * Converts from fake LibEvent type to actual libevent type.
 * Used to avoid polluting the global namespace.
 */
inline event_base*
unqualify(LibEvent::event_base* base)
{
    return reinterpret_cast<event_base*>(base);
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

#endif /* LOGCABIN_EVENT_INTERNAL_H */
