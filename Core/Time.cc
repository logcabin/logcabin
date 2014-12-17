/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014 Diego Ongaro
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

#include <cassert>
#include <cstring>

#include "Core/Debug.h"
#include "Core/Time.h"

namespace LogCabin {
namespace Core {
namespace Time {

CSystemClock::time_point
CSystemClock::now()
{
    return time_point(std::chrono::nanoseconds(getTimeNanos()));
}


CSteadyClock::time_point
CSteadyClock::now()
{
    struct timespec now;
    int r = clock_gettime(STEADY_CLOCK_ID, &now);
    if (r != 0) {
        PANIC("clock_gettime(STEADY_CLOCK_ID=%d) failed: %s",
              STEADY_CLOCK_ID, strerror(errno));
    }
    return time_point(std::chrono::nanoseconds(
                uint64_t(now.tv_sec) * 1000 * 1000 * 1000 +
                now.tv_nsec));
}

uint64_t
getTimeNanos()
{
    struct timespec now;
    int r = clock_gettime(CLOCK_REALTIME, &now);
    if (r != 0) {
        PANIC("clock_gettime(CLOCK_REALTIME) failed: %s",
              strerror(errno));
    }
    return uint64_t(now.tv_sec) * 1000 * 1000 * 1000 + now.tv_nsec;
}

} // namespace LogCabin::Core::Time
} // namespace LogCabin::Core
} // namespace LogCabin
