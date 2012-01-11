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

#include <gtest/gtest.h>
#include <unistd.h>

#include "Debug.h"
#include "WorkDispatcher.h"

namespace DLog {

namespace {

class CompletionCallback : public WorkDispatcher::CompletionCallback  {
    CompletionCallback()
        : count(0)
    {
    }
  public:
    void completed() {
        ++count;
    }
    uint32_t count;
    friend class MakeHelper;
    friend class RefHelper<CompletionCallback>;
};

class CompletionNotifier : public WorkDispatcher::CompletionNotifier  {
    CompletionNotifier()
        : count(0)
    {
    }
  public:
    void notify() {
        ++count;
    }
    uint32_t count;
    friend class MakeHelper;
    friend class RefHelper<CompletionCallback>;
};

class WorkerCallback : public WorkDispatcher::WorkerCallback  {
    WorkerCallback()
        : count(0)
    {
    }
  public:
    void run() {
        ++count;
    }
    uint32_t count;
    friend class MakeHelper;
    friend class RefHelper<WorkerCallback>;
};

} // anonymous namespace

namespace WorkDispatcherDebug {
extern uint32_t totalNumWorkers;
}

class WorkDispatcherTest : public ::testing::Test {
  public:
    WorkDispatcherTest()
        : notifier(make<CompletionNotifier>())
        , completion(make<CompletionCallback>())
        , work(make<WorkerCallback>())
    {
        // Temporarily delete the global workDispatcher to be sure that
        // WOrkDispatcherDebug::totalNumWorkers starts at 0.
        delete workDispatcher;
        workDispatcher = NULL;
    }

    ~WorkDispatcherTest() {
        // Restore the global workDispatcher.
        workDispatcher = new WorkDispatcher(0, 32);
    }

    Ref<CompletionNotifier> notifier;
    Ref<CompletionCallback> completion;
    Ref<WorkerCallback> work;
};


TEST_F(WorkDispatcherTest, constructor) {
    WorkDispatcher dispatcher(2, 4);
    EXPECT_EQ(4U, dispatcher.maxThreads);
    // wait for threads to spawn
    usleep(1000);
    if (dispatcher.numFreeWorkers != 2)
        usleep(20 * 1000);
    EXPECT_EQ(2U, dispatcher.numWorkers);
    EXPECT_EQ(2U, dispatcher.numFreeWorkers);
}

TEST_F(WorkDispatcherTest, destructor) {
    {
        WorkDispatcher dispatcher(2, 4);
        // wait for threads to spawn
        usleep(1000);
        if (WorkDispatcherDebug::totalNumWorkers != 2)
            usleep(20 * 1000);
        EXPECT_EQ(2U, WorkDispatcherDebug::totalNumWorkers);
        dispatcher.setNotifier(notifier);
        dispatcher.scheduleCompletion(completion);
        dispatcher.scheduleCompletion(completion);
        dispatcher.scheduleCompletion(completion);
    }
    EXPECT_EQ(0U, WorkDispatcherDebug::totalNumWorkers);
    EXPECT_EQ(3U, completion->count);
    EXPECT_EQ(3U, notifier->count);
}

TEST_F(WorkDispatcherTest, setNotifier) {
    WorkDispatcher dispatcher(0, 4);
    dispatcher.scheduleCompletion(completion);
    dispatcher.setNotifier(notifier);
    EXPECT_EQ(1U, notifier->count);
    dispatcher.scheduleCompletion(completion);
    EXPECT_EQ(2U, notifier->count);
}

TEST_F(WorkDispatcherTest, scheduleWork) {
    WorkDispatcher dispatcher(0, 1);
    dispatcher.scheduleWork(work);
    // wait for threads to execute work
    usleep(1000);
    if (work->count != 1)
        usleep(20 * 1000);
    EXPECT_EQ(1U, work->count);
    EXPECT_EQ(1U, dispatcher.numWorkers);
}

TEST_F(WorkDispatcherTest, scheduleCompletion) {
    WorkDispatcher dispatcher(0, 4);
    dispatcher.scheduleCompletion(completion);
    dispatcher.setNotifier(notifier);
    notifier->count = 0;
    dispatcher.scheduleCompletion(completion);
    EXPECT_EQ(2U, dispatcher.completionQueue.size());
    EXPECT_EQ(1U, notifier->count);
}

TEST_F(WorkDispatcherTest, popCompletion) {
    WorkDispatcher dispatcher(0, 1);
    EXPECT_FALSE(dispatcher.popCompletion());
    dispatcher.scheduleCompletion(completion);
    dispatcher.scheduleCompletion(completion);
    EXPECT_TRUE(dispatcher.popCompletion());
    EXPECT_TRUE(dispatcher.popCompletion());
    EXPECT_FALSE(dispatcher.popCompletion());
}

TEST_F(WorkDispatcherTest, spawnThread) {
    WorkDispatcher dispatcher(0, 1);
    dispatcher.spawnThread();
    dispatcher.spawnThread();
    EXPECT_EQ(1U, dispatcher.numWorkers);
}

TEST_F(WorkDispatcherTest, workerMain) {
    // Hopefully this is tested sufficiently in the other tests;
    // it's hard to test directly.
}

} // namespace DLog
