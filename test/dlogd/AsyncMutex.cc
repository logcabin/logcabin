/* Copyright (c) 2011 Stanford University
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

#include <gtest/gtest.h>

#include "dlogd/AsyncMutex.h"
#include "dlogd/LogManager.h"

namespace DLog {

namespace {

class TestCallback : public AsyncMutex::Callback {
  private:
    TestCallback() : count(0) {}
  public:
    void acquired() {
        ++count;
    }
    uint32_t count;
    friend class RefHelper<TestCallback>;
    friend class MakeHelper;
};

} // anonymous namespace

TEST(AsyncMutex, basic) {
    Ref<TestCallback> cb = make<TestCallback>();
    AsyncMutex mutex;
    mutex.acquire(cb); // should fire right away
    EXPECT_EQ(1U, cb->count);
    EXPECT_TRUE(mutex.locked);
    mutex.acquire(cb); // should be queued
    EXPECT_EQ(1U, cb->count);
    EXPECT_EQ(1U, mutex.queued.size());
    EXPECT_TRUE(mutex.locked);
    mutex.release(); // should fire the queued callback
    EXPECT_EQ(2U, cb->count);
    EXPECT_EQ(0U, mutex.queued.size());
    EXPECT_TRUE(mutex.locked);
    mutex.release(); // should unlock
    EXPECT_EQ(2U, cb->count);
    EXPECT_EQ(0U, mutex.queued.size());
    EXPECT_FALSE(mutex.locked);
}

} // namespace DLog
