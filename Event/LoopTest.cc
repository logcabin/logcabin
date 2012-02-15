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

#include <event2/event.h>
#include <gtest/gtest.h>

#include "Internal.h"
#include "Loop.h"

namespace LogCabin {
namespace Event {
namespace {

uint32_t fired;
void
exitEventLoop(evutil_socket_t fd, short events, void* loop) // NOLINT
{
    ++fired;
    static_cast<Loop*>(loop)->exit();
}

TEST(EventLoopTest, constructor) {
    Loop loop;
    // nothing to test
}

TEST(EventLoopTest, destructor) {
    Loop loop;
    // nothing to test
}

TEST(EventLoopTest, runForever) {
    Loop loop;
    // This should actually return right away since no events have been set up.
    // Unfortunately, if it doesn't, it's going to fail in an obnoxious way:
    // by never returning.
    loop.runForever();
}

TEST(EventLoopTest, exit) {
    fired = 0;
    struct timeval timeout {0, 0};
    Loop loop;
    event* timer = evtimer_new(unqualify(loop.base),
                               exitEventLoop, &loop);
    EXPECT_EQ(0, evtimer_add(timer, &timeout));
    loop.runForever();
    EXPECT_EQ(1U, fired);
    event_free(timer);
}

} // namespace LogCabin::Event::<anonymous>
} // namespace LogCabin::Event
} // namespace LogCabin
