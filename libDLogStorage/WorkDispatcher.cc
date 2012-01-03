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

#include <string.h>
#include <unistd.h>

#include "WorkDispatcher.h"
#include "../dlogd/Debug.h" // TODO(ongaro): Move Debug to common

namespace DLog {

#if DEBUG
/// Used in unit tests.
namespace WorkDispatcherDebug {
/**
 * The total number of workers in any WorkDispatcher.
 * Not globally thread-safe, but good enough for testing purposes.
 */
uint32_t totalNumWorkers;
}
#endif

namespace {
class WorkerExitSentinel : public WorkDispatcher::WorkerCallback {
  public:
    void run() {
        PANIC("BUG: Shouldn't get here!");
    }
    friend class MakeHelper;
    friend class RefHelper<WorkerExitSentinel>;
};
const Ref<WorkerExitSentinel> workerExitSentinel = make<WorkerExitSentinel>();

} // anonymous namespace

WorkDispatcher::WorkDispatcher(uint32_t minThreads, uint32_t maxThreads)
    : maxThreads(maxThreads)
    , mutex()
    , conditionVariable()
    , workQueue()
    , completionQueue()
    , numWorkers(0)
    , numFreeWorkers(0)
    , completionNotifier()
{
    assert(minThreads <= maxThreads);
    assert(0 < maxThreads);
    for (uint32_t i = 0; i < minThreads; ++i)
        spawnThread();
}

WorkDispatcher::~WorkDispatcher()
{
    // Schedule workers to exit.
    do {
        LockGuard lock(mutex);
        if (workQueue.empty()) {
            completionNotifier.reset();
            for (uint32_t i = 0; i < numWorkers; ++i)
                workQueue.push(workerExitSentinel);
            conditionVariable.notify_all();
            break;
        }
    } while (usleep(1000), true);

    // Wait for workers to exit.
    do {
        LockGuard lock(mutex);
        if (numWorkers == 0)
            break;
    } while (usleep(1000), true);

    // No workers are running, so don't worry about holding the mutex.

    // Call remaining completions.
    while (true) {
        Ptr<CompletionCallback> completion = popCompletion();
        if (!completion)
            break;
        completion->completed();
    }
}

void
WorkDispatcher::setNotifier(Ptr<CompletionNotifier> notifier)
{
    LockGuard lock(mutex);
    completionNotifier = notifier;
    if (completionNotifier && !completionQueue.empty())
        completionNotifier->notify();
}

void
WorkDispatcher::scheduleWork(Ref<WorkerCallback> work)
{
    { // queue work
        LockGuard lock(mutex);
        workQueue.push(work);
        if (numFreeWorkers == 0)
            spawnThread();
    }
    // notify worker
    conditionVariable.notify_one();
}

void
WorkDispatcher::scheduleCompletion(Ref<CompletionCallback> completion)
{
    Ptr<CompletionNotifier> notifier;
    {
        LockGuard lock(mutex);
        completionQueue.push(completion);
        notifier = completionNotifier;
    }
    if (notifier)
        notifier->notify();
}

Ptr<WorkDispatcher::CompletionCallback>
WorkDispatcher::popCompletion()
{
    LockGuard lock(mutex);
    Ptr<CompletionCallback> completion;
    if (!completionQueue.empty()) {
        completion = completionQueue.front();
        completionQueue.pop();
    }
    return completion;
}

void
WorkDispatcher::spawnThread()
{
    if (numWorkers == maxThreads)
        return;
    ++numWorkers;
#if DEBUG
    ++WorkDispatcherDebug::totalNumWorkers;
#endif
    std::thread thread(&WorkDispatcher::workerMain, this);
    thread.detach();
}

void
WorkDispatcher::workerMain()
{
    while (true) {
        Ptr<WorkerCallback> work;
        { // find work
            LockGuard lock(mutex);
            if (workQueue.empty()) {
                ++numFreeWorkers;
                do {
                    conditionVariable.wait(lock);
                } while (workQueue.empty());
                --numFreeWorkers;
            }
            work = workQueue.front();
            workQueue.pop();
            if (work == workerExitSentinel) {
                --numWorkers;
#if DEBUG
                --WorkDispatcherDebug::totalNumWorkers;
#endif
                return;
            }
        }
        // execute work
        work->run();
    }
}

WorkDispatcher* workDispatcher = new WorkDispatcher(0, 32);

} // namespace DLog
