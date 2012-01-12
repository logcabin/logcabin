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


class EventTimerTest : public ::testing::Test {
  public:
    EventTimerTest()
        : loop(NULL)
    {
        loop = RPC::EventLoop::makeEventLoop();
    }
    ~EventTimerTest() {
        delete loop;
    }
    RPC::EventLoop *loop;
  private:
    EventTimerTest(const EventTimerTest&) = delete;
    EventTimerTest& operator=(const EventTimerTest&) = delete;
};

class MyTimer : public RPC::EventTimer {
  public:
    explicit MyTimer(RPC::EventLoop &loop)
        : RPC::EventTimer(loop),
          triggerCount(0),
          loopPtr(&loop)
    {
    }
    virtual ~MyTimer()
    {
    }
    virtual void trigger() {
        triggerCount += 1;
        if (triggerCount == 10)
            loopPtr->loopBreak();
    }
    int triggerCount;
    RPC::EventLoop *loopPtr;
    MyTimer(const MyTimer&) = delete;
    MyTimer& operator=(const MyTimer&) = delete;
};

TEST_F(EventTimerTest, addPeriodic) {
    MyTimer timer(*loop);
    timer.addPeriodic(0);
    EXPECT_TRUE(timer.isPending());
    EXPECT_EQ(0, timer.triggerCount);
    loop->processEvents();
    EXPECT_EQ(10, timer.triggerCount);
}

TEST_F(EventTimerTest, add) {
    MyTimer timer(*loop);
    timer.add(0);
    EXPECT_TRUE(timer.isPending());
    EXPECT_EQ(0, timer.triggerCount);
    loop->processEvents();
    EXPECT_EQ(1, timer.triggerCount);
}

TEST_F(EventTimerTest, remove) {
    MyTimer timer(*loop);
    timer.add(0);
    timer.remove();
    EXPECT_FALSE(timer.isPending());
    EXPECT_EQ(0, timer.triggerCount);
}

} // namespace DLog
