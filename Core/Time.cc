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

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <stdexcept>

#include "Core/Debug.h"
#include "Core/Time.h"

namespace LogCabin {
namespace Core {
namespace Time {

CSystemClock::time_point
CSystemClock::now()
{
    struct timespec now;
    int r = clock_gettime(CLOCK_REALTIME, &now);
    if (r != 0) {
        PANIC("clock_gettime(CLOCK_REALTIME) failed: %s",
              strerror(errno));
    }
    return time_point(std::chrono::nanoseconds(
                int64_t(now.tv_sec) * 1000 * 1000 * 1000 +
                now.tv_nsec));
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

uint64_t
parseDuration(const std::string& description)
{
    const char* start = description.c_str();
    char* end = NULL;
    errno = 0;
    uint64_t r = strtoul(start, &end, 10);
    if (errno != 0 || start == end) {
        throw std::runtime_error(
            std::string("Invalid time description: "
                        "could not parse number from ") + description);
    }

    std::string units = end;
    // The black magic is from https://stackoverflow.com/a/217605
    // trim whitespace at end of string
    units.erase(std::find_if(units.rbegin(), units.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace)))
                .base(),
            units.end());
    // trim whitespace at beginning of string
    units.erase(units.begin(),
            std::find_if(units.begin(), units.end(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))));

    if (units == "ns") {
        // pass
    } else if (units == "us") {
        r *= 1000UL;
    } else if (units == "ms") {
        r *= 1000000UL;
    } else if (units == "s" || units == "") {
        r *= 1000000000UL;
    } else {
        throw std::runtime_error(
            std::string("Invalid time description: "
                        "could not parse units from ") + description);
    }
    return r;
}

void
sleep(SteadyClock::time_point wake)
{
    struct timespec wakeSpec = makeTimeSpec(wake);
    if (wakeSpec.tv_sec < 0)
        return;
    int r = clock_nanosleep(STEADY_CLOCK_ID,
                            TIMER_ABSTIME,
                            &wakeSpec,
                            NULL);
    if (r != 0) {
        PANIC("clock_nanosleep(STEADY_CLOCK_ID=%d, %s) failed: %s",
              STEADY_CLOCK_ID,
              Core::StringUtil::toString(wake).c_str(),
              strerror(r));
    }
}

SteadyTimeConverter::SteadyTimeConverter()
    : steadyNow(SteadyClock::now())
    , systemNow(SystemClock::now())
{
}

SystemClock::time_point
SteadyTimeConverter::convert(SteadyClock::time_point when)
{
    // Note that this relies on signed integer wrapping, so -fwrapv or
    // -fno-strict-overflow must be on for correctness under optimizing
    // comiplers. The unit tests are pretty good at catching when this isn't
    // the case.
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

namespace std {

namespace {

std::string
padFraction(int64_t fraction, uint64_t digits)
{
    if (fraction == 0)
        return "";
    if (fraction < 0)
        fraction *= -1;
    while (fraction % 1000 == 0 && digits >= 3) {
        digits -= 3;
        fraction /= 1000;
    }
    char ret[digits + 2];
    ret[0] = '.';
    ret[digits + 1] = '\0';
    for (uint64_t i = digits; i > 0; --i) {
        ret[i] = char('0' + (fraction % 10));
        fraction /= 10;
    }
    return ret;
}

} // namespace std::<anonymous>

std::ostream&
operator<<(std::ostream& os,
           const std::chrono::nanoseconds& duration)
{
    int64_t nanos = duration.count();
    if (nanos / 1000000000L != 0) {
        int64_t whole    = nanos / 1000000000L;
        int64_t fraction = nanos % 1000000000L;
        os << whole << padFraction(fraction, 9) << " s";
    } else if (nanos / 1000000L != 0) {
        int64_t whole    = nanos / 1000000L;
        int64_t fraction = nanos % 1000000L;
        os << whole << padFraction(fraction, 6) << " ms";
    } else if (nanos / 1000L != 0) {
        int64_t whole    = nanos / 1000L;
        int64_t fraction = nanos % 1000L;
        os << whole << padFraction(fraction, 3) << " us";
    } else {
        os << nanos << " ns";
    }
    return os;
}

std::ostream&
operator<<(std::ostream& os,
           const std::chrono::microseconds& duration)
{
    return os << chrono::duration_cast<chrono::nanoseconds>(duration);
}

std::ostream&
operator<<(std::ostream& os,
           const std::chrono::milliseconds& duration)
{
    return os << chrono::duration_cast<chrono::nanoseconds>(duration);
}

std::ostream&
operator<<(std::ostream& os,
           const std::chrono::seconds& duration)
{
    return os << chrono::duration_cast<chrono::nanoseconds>(duration);
}

std::ostream&
operator<<(std::ostream& os,
           const std::chrono::minutes& duration)
{
    return os << chrono::duration_cast<chrono::nanoseconds>(duration);
}

std::ostream&
operator<<(std::ostream& os,
           const std::chrono::hours& duration)
{
    return os << chrono::duration_cast<chrono::nanoseconds>(duration);
}

} // namespace std
