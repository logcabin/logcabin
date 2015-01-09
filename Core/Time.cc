/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014-2015 Diego Ongaro
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
                int64_t(now.tv_sec) * 1000 * 1000 * 1000 +
                now.tv_nsec));
}

int64_t
getTimeNanos()
{
    struct timespec now;
    int r = clock_gettime(CLOCK_REALTIME, &now);
    if (r != 0) {
        PANIC("clock_gettime(CLOCK_REALTIME) failed: %s",
              strerror(errno));
    }
    return int64_t(now.tv_sec) * 1000 * 1000 * 1000 + now.tv_nsec;
}

SteadyTimeConverter::SteadyTimeConverter()
    : steadyNow(SteadyClock::now())
    , systemNow(SystemClock::now())
{
}

SystemClock::time_point
SteadyTimeConverter::convert(SteadyClock::time_point when)
{
    std::chrono::nanoseconds diff = when - steadyNow;
    SystemClock::time_point then = systemNow + diff;
    if (when > steadyNow && then < systemNow) // overflow
        return SystemClock::time_point::max();
    return then;
}

int64_t
SteadyTimeConverter::unixNanos(SteadyClock::time_point when)
{
    return std::chrono::nanoseconds(
        convert(when).time_since_epoch()).count();
}


} // namespace LogCabin::Core::Time
} // namespace LogCabin::Core
} // namespace LogCabin
