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

#include "Core/Debug.h"
#include "Event/Internal.h"

#include <event2/thread.h>
#include <event2/util.h>

namespace LibEvent {
namespace {

/**
 * This function runs before main() to initialize the libevent library.
 *
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

    r = evutil_secure_rng_init();
    if (r == -1) {
        PANIC("evutil_secure_rng_init failed: "
              "The random number generator couldn't be seeded.");
    }

    return true;
}

} // anonymous namespace

const bool initialized = init();

} // namespace LibEvent
