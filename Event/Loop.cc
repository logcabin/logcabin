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

#include "include/Debug.h"
#include "Internal.h"
#include "Loop.h"

#include <event2/event.h>
#include <event2/thread.h>

namespace LogCabin {
namespace Event {

namespace {

/**
 * This function runs before main().
 * Thanks to libevent bug #3479058, it's best to run evthread_use_pthreads at
 * initialization time. Calling it twice during execution of the program (or
 * tests) results in uninitialized memory accesses in libevent.
 */
bool
init()
{
    int r = evthread_use_pthreads();
    if (r == -1) {
        PANIC("evthread_use_pthreads failed: "
              "Make sure you've built libevent with pthreads support "
              "and linked against libevent_pthreads as well as libevent.");
    }
    return true;
}

/**
 * This will always have the value true.
 * Its purpose is to call init() during initialization.
 */
bool dummy = init();

} // anonymous namespace

Loop::Loop()
    : base(NULL)
{
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
