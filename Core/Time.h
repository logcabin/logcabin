/* Copyright (c) 2011-2012 Stanford University
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

#include <chrono>
#include <iostream>
#include "Core/StringUtil.h"

#ifndef LOGCABIN_CORE_TIME_H
#define LOGCABIN_CORE_TIME_H

namespace std {

/**
 * Prints std::time_point values in a way that is useful for unit tests.
 */
template<typename Clock, typename Duration>
std::ostream&
operator<<(std::ostream& os,
           const std::chrono::time_point<Clock, Duration>& timePoint) {
    typedef std::chrono::time_point<Clock, Duration> TimePoint;
    using LogCabin::Core::StringUtil::format;

    if (timePoint == TimePoint::min())
        return os << "TimePoint::min()";
    if (timePoint == TimePoint::max())
        return os << "TimePoint::max()";

    TimePoint unixEpoch = TimePoint();
    uint64_t microsSinceUnixEpoch =
        std::chrono::duration_cast<std::chrono::microseconds>(
                timePoint - unixEpoch).count();
    return os << format("%lu.%06lu",
                        microsSinceUnixEpoch / 1000000,
                        microsSinceUnixEpoch % 1000000);
}

}

namespace LogCabin {
namespace Core {
namespace Time {

/**
 * Reads the current time. This time may not correspond to wall time, depending
 * on the underlying BaseClock. This class gives unit tests a way to fake the
 * current time.
 */
template<typename BaseClock>
struct MockableClock
{
  typedef typename BaseClock::duration duration;
  typedef typename BaseClock::rep rep;
  typedef typename BaseClock::period period;
  typedef typename BaseClock::time_point time_point;

// libstdc++ 4.7 renamed monotonic_clock to steady_clock to conform with C++11.
#if __GNUC__ == 4 && __GNUC_MINOR__ < 7
  static const bool is_monotonic = BaseClock::is_monotonic;
#else
  static const bool is_steady = BaseClock::is_steady;
#endif

  static time_point now() {
      if (useMockValue)
          return mockValue;
      else
          return BaseClock::now();
  }

  static bool useMockValue;
  static time_point mockValue;
};

template<typename BaseClock>
bool
MockableClock<BaseClock>::useMockValue = false;

template<typename BaseClock>
typename MockableClock<BaseClock>::time_point
MockableClock<BaseClock>::mockValue;

/**
 * The best available clock on this system for uses where a steady, monotonic
 * clock is desired. Unless you're compiling this well into the future, this is
 * most likely a non-steady, non-monotonic clock that is affected by changes to
 * the system time.
 */
// libstdc++ 4.7 renamed monotonic_clock to steady_clock to conform with C++11.
#if __GNUC__ == 4 && __GNUC_MINOR__ < 7
typedef MockableClock<std::chrono::monotonic_clock> SteadyClock;
#else
typedef MockableClock<std::chrono::steady_clock> SteadyClock;
#endif

/**
 * A clock that reads wall time.
 */
typedef MockableClock<std::chrono::system_clock> SystemClock;

/**
 * Read the CPU's cycle counter.
 * This is useful for benchmarking.
 */
static __inline __attribute__((always_inline))
uint64_t
rdtsc()
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
    return (((uint64_t)hi << 32) | lo);
}

} // namespace LogCabin::Core::Time
} // namespace LogCabin::Core
} // namespace LogCabin

#endif /* LOGCABIN_CORE_TIME_H */
