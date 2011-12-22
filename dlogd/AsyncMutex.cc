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

#include "AsyncMutex.h"

namespace DLog {

AsyncMutex::AsyncMutex()
    : locked(false)
    , queued()
{
}

AsyncMutex::~AsyncMutex()
{
    assert(!locked);
    assert(queued.empty());
}

void
AsyncMutex::acquire(Ref<Callback> callback)
{
    if (locked) {
        queued.push(callback);
    } else {
        locked = true;
        callback->acquired();
    }
}

void
AsyncMutex::release()
{
    assert(locked);
    if (queued.empty()) {
        locked = false;
    } else {
        Ref<Callback> callback = queued.front();
        queued.pop();
        callback->acquired();
    }
}

} // namespace DLog
