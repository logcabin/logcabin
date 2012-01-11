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

#include <signal.h>

#include <gtest/gtest.h>

#include "Debug.h"
#include "DLogEvent.h"

namespace DLog {


class EventSignalTest : public ::testing::Test {
  public:
    EventSignalTest()
        : loop(NULL)
    {
        loop = RPC::EventLoop::makeEventLoop();
    }
    ~EventSignalTest() {
        delete loop;
    }
    RPC::EventLoop *loop;
  private:
    EventSignalTest(const EventSignalTest&) = delete;
    EventSignalTest& operator=(const EventSignalTest&) = delete;
};

class MySignal : public RPC::EventSignal {
  public:
    MySignal(RPC::EventLoop &loop, int s)
        : RPC::EventSignal(loop, s),
          caughtSignal(false)
    {
    }
    virtual ~MySignal()
    {
    }
    virtual void trigger() {
        caughtSignal = true;
    }
    bool caughtSignal;
};

TEST_F(EventSignalTest, add) {
    MySignal signal(*loop, SIGUSR1);
    signal.add();
    EXPECT_TRUE(signal.isPending());
    EXPECT_FALSE(signal.caughtSignal);
}

TEST_F(EventSignalTest, addTimeout) {
    MySignal signal(*loop, SIGUSR1);
    signal.add(0);
    EXPECT_TRUE(signal.isPending());
    EXPECT_FALSE(signal.caughtSignal);
    loop->processEvents();
    EXPECT_TRUE(signal.caughtSignal);
}

TEST_F(EventSignalTest, remove) {
    MySignal signal(*loop, SIGUSR1);
    signal.add();
    signal.remove();
    EXPECT_FALSE(signal.isPending());
    EXPECT_FALSE(signal.caughtSignal);
}

TEST_F(EventSignalTest, getSignal) {
    MySignal signal(*loop, SIGUSR1);
    EXPECT_EQ(SIGUSR1, signal.getSignal());
    EXPECT_FALSE(signal.caughtSignal);
}

} // namespace DLog
