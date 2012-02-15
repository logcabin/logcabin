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

#include <assert.h>
#include <event2/event.h>
#include <event2/thread.h>

#include "include/Debug.h"
#include "Internal.h"
#include "Loop.h"

namespace LogCabin {
namespace Event {

Loop::Loop()
    : base(NULL)
{
    assert(LibEvent::initialized);
    base = qualify(event_base_new());
    if (base == NULL) {
        PANIC("event_base_new failed: "
              "No information is available from libevent about this error.");
    }
}

Loop::~Loop()
{
    event_base_free(unqualify(base));
}

void
Loop::runForever()
{
    int r = event_base_dispatch(unqualify(base));
    if (r == -1) {
        PANIC("event_loop_dispatch failed: "
              "No information is available from libevent about this error.");
    }
}

void
Loop::exit()
{
    int r = event_base_loopbreak(unqualify(base));
    if (r == -1) {
        PANIC("event_loop_break failed: "
              "No information is available from libevent about this error.");
    }
}

} // namespace LogCabin::Event
} // namespace LogCabin
