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
 * Contains WorkDispatcher class.
 */

#include <pthread.h>
#include <queue>
#include <thread>
#include <vector>

#include "Callback.h"

#ifndef WORKDISPATCHER_H
#define WORKDISPATCHER_H

namespace DLog {

/**
 * Manages a thread pool on which to offload work.
 * The thread(s) which are offloading work to this thread pool are referred to
 * as the 'client' of this class.
 *
 * For now, there is a single, global instance of this class named
 * 'workDispatcher'.
 */
class WorkDispatcher {
  public:
    /**
     * A unit of work for some worker to execute.
     */
    class WorkerCallback : public BaseCallback {
      public:
        virtual void run() = 0;
      protected:
        friend class RefHelper<WorkerCallback>;
    };

    /**
     * A callback that is executed on the client's thread once the worker is
     * done.
     */
    class CompletionCallback : public BaseCallback {
      public:
        virtual void completed() = 0;
      protected:
        friend class RefHelper<CompletionCallback>;
    };

    /**
     * How a worker thread notifies the client's thread of completing work.
     */
    class CompletionNotifier : public BaseCallback {
      public:
        /**
         * This should somehow trigger the client's thread to call
         * popCompletion().
         */
        virtual void notify() = 0;
      protected:
        friend class RefHelper<CompletionNotifier>;
        friend class MakeHelper;
    };

    /**
     * Constructor.
     * \param minThreads
     *      The number of threads with which to start the thread pool.
     *      These will be created in the constructor.
     * \param maxThreads
     *      The maximum number of threads this class is allowed to use for its
     *      thread pool. The thread pool dynamically grows as needed up until
     *      this limit. This should be set to at least 'minThreads' and more
     *      than 0.
     */
    WorkDispatcher(uint32_t minThreads, uint32_t maxThreads);

    /**
     * Destructor.
     * This will wait until there is no more work to do and all threads have
     * exited. If there are remaining client completions to run, it will run
     * them directly.
     */
    ~WorkDispatcher();

    /**
     * Set up the callback to be executed on worker threads to notify the
     * client's thread(s) of completing work. This should normally be called
     * just once during initialization.
     *
     * It will replace an existing callback if previously set. In this case,
     * the prior callback may still receive notifications for a short period of
     * time.
     */
    void setNotifier(Ptr<CompletionNotifier> notifier);

    /**
     * Queue up work for a worker thread.
     * \param work
     *      The work to be executed on the worker thread. If the client cares
     *      about when this completes, this work should end with a call to
     *      scheduleCompletion().
     */
    void scheduleWork(Ref<WorkerCallback> work);

    /**
     * Queue a completion to be executed on the client's thread.
     * This is intended to be called from the context of a WorkerCallback.
     */
    void scheduleCompletion(Ref<CompletionCallback> completion);

    /**
     * Get a completion to execute on the client's thread.
     * This should normally be called as the result of a CompletionNotifier.
     * \return
     *      A completion to execute on the client's thread, or NULL if there
     *      are no current completions to execute.
     */
    Ptr<CompletionCallback> popCompletion();

  private:
    /**
     * Spawn a new worker thread. If maxThreads number of threads have already
     * been spawned, this will do nothing.
     * The caller must hold the mutex.
     */
    void spawnThread();

    /**
     * The main loop executed in workers.
     */
    void workerMain();

    /**
     * See constructor.
     */
    const uint32_t maxThreads;

    /**
     * An RAII wrapper for holding locks.
     */
    typedef std::unique_lock<std::mutex> LockGuard;

    /**
     * A lock which covers all other members of this class.
     */
    std::mutex mutex;

    /**
     * Notifies workers of available work.
     * To wait on this, one needs to hold 'mutex'.
     */
    std::condition_variable conditionVariable;

    /**
     * The queue of work that worker threads pull from.
     */
    std::queue<Ref<WorkerCallback>> workQueue;

    /**
     * The queue of completions that the client pulls from.
     */
    std::queue<Ref<CompletionCallback>> completionQueue;

    /**
     * The number of workers in the thread pool.
     */
    uint32_t numWorkers;

    /**
     * The number of workers that are waiting for work (on the condition
     * variable). This is used to dynamically launch new workers when
     * necessary.
     */
    uint32_t numFreeWorkers;

    /**
     * See setNotifier.
     */
    Ptr<CompletionNotifier> completionNotifier;

    WorkDispatcher(const WorkDispatcher&) = delete;
    WorkDispatcher& operator=(const WorkDispatcher&) = delete;
};

/**
 * For now, there is a single, global instance of this class.
 */
// TODO(ongaro): remove global (ha!)
extern WorkDispatcher* workDispatcher;

} // namespace DLog

#endif /* WORKDISPATCHER_H */
