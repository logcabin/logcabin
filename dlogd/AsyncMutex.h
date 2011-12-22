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

#include <queue>

#include "DLogStorage.h"
#include "Ref.h"

/**
 * \file
 * Contains the AsyncMutex class.
 */

#ifndef DLOGD_ASYNCMUTEX_H
#define DLOGD_ASYNCMUTEX_H

namespace DLog {

/**
 * Provides mutual exclusion for tasks that may yield the thread because they
 * have asynchronous steps.
 */
class AsyncMutex {
  public:
    /**
     * See acquire().
     */
    class Callback : public BaseCallback {
      public:
        virtual void acquired() = 0;
        friend class RefHelper<Callback>;
    };

    AsyncMutex();
    ~AsyncMutex();

    /**
     * Acquire the mutex.
     * \param callback
     *      Called once no one else is holding the mutex (often immediately).
     *      The callback must arrange for a call to release() to happen
     *      eventually.
     */
    void acquire(Ref<Callback> callback);

    /**
     * Release the mutex.
     */
    void release();

  private:
    /**
     * Whether anyone is holding the mutex in a critical section.
     */
    bool locked;
    /**
     * A set of other tasks that are waiting for the mutex to be released.
     */
    std::queue<Ref<Callback>> queued;
};

} // namespace DLog

#endif /* DLOGD_ASYNCMUTEX_H */
