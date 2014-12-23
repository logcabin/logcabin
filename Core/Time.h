/* Copyright (c) 2011-2012 Stanford University
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

#include <chrono>
#include <iostream>
#include <time.h>

#include "Core/StringUtil.h"

#ifndef LOGCABIN_CORE_TIME_H
#define LOGCABIN_CORE_TIME_H

namespace std {

/**
 * Prints std::milliseconds values in a way that is useful for unit tests.
 */
inline std::ostream&
operator<<(std::ostream& os,
           const std::chrono::milliseconds& duration) {
    return os << duration.count() << " ms";
}

/**
 * Prints std::nanoseconds values in a way that is useful for unit tests.
 */
inline std::ostream&
operator<<(std::ostream& os,
           const std::chrono::nanoseconds& duration) {
    return os << duration.count() << " ns";
}


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

    struct timespec ts = makeTimeSpec(timePoint);
    return os << format("%ld.%09ld", ts.tv_sec, ts.tv_nsec);
}

}

namespace LogCabin {
namespace Core {
namespace Time {

/**
 * Convert a C++11 time point into a POSIX timespec.
 * \param when
 *      Time point to convert.
 * \return
 *      Time in seconds and nanoseconds relative to the Clock's epoch.
 */
template<typename Clock, typename Duration>
struct timespec
makeTimeSpec(const std::chrono::time_point<Clock, Duration>& when)
{
    std::chrono::nanoseconds::rep nanosSinceEpoch =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
           when.time_since_epoch()).count();
    struct timespec ts;
    ts.tv_sec  = nanosSinceEpoch / 1000000000L;
    ts.tv_nsec = nanosSinceEpoch % 1000000000L;
    // tv_nsec must always be in range [0, 1e9)
    if (nanosSinceEpoch < 0) {
        ts.tv_sec  -= 1;
        ts.tv_nsec += 1000000000L;
    }
    return ts;
}

/**
 * Wall clock in nanosecond granularity.
 * Wrapper around clock_gettime(CLOCK_REALTIME).
 * Usually, you'll want to access this through #SystemClock.
 *
 * This is preferred over std::chrono::system_clock for earlier libstdc++
 * versions, since those use only a microsecond granularity.
 */
struct CSystemClock {
  typedef std::chrono::nanoseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef std::chrono::time_point<CSystemClock, duration> time_point;

  // libstdc++ 4.7 renamed monotonic_clock to steady_clock to conform with
  // C++11. This class defines both, since it's free.
  static const bool is_monotonic = false;
  static const bool is_steady = false;

  static time_point now();
};

/**
 * The clock used by CSteadyClock. For now (2014), we can't use
 * CLOCK_MONOTONIC_RAW in condition variables since glibc doesn't support that,
 * so stick with CLOCK_MONOTONIC. This rate of this clock may change due to NTP
 * adjustments, but at least it won't jump.
 */
const clockid_t STEADY_CLOCK_ID = CLOCK_MONOTONIC;

/**
 * Monotonic clock in nanosecond granularity.
 * Wrapper around clock_gettime(STEADY_CLOCK_ID = CLOCK_MONOTONIC).
 * Usually, you'll want to access this through #SteadyClock.
 *
 * This is preferred over std::chrono::monotonic_clock and
 * std::chrono::steady_clock for earlier libstdc++ versions, since those use
 * are not actually monotonic (they're typedefed to system_clock).
 */
struct CSteadyClock {
  typedef std::chrono::nanoseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef std::chrono::time_point<CSteadyClock, duration> time_point;

  // libstdc++ 4.7 renamed monotonic_clock to steady_clock to conform with
  // C++11. This class defines both, since it's free.
  static const bool is_monotonic = true;
  static const bool is_steady = true;

  static time_point now();
};


/**
 * Reads the current time. This time may not correspond to wall time, depending
 * on the underlying BaseClock. This class gives unit tests a way to fake the
 * current time.
 */
template<typename _BaseClock>
struct MockableClock
{
  typedef _BaseClock BaseClock;
  typedef typename BaseClock::duration duration;
  typedef typename BaseClock::rep rep;
  typedef typename BaseClock::period period;
  typedef typename BaseClock::time_point time_point;

// libstdc++ 4.7 renamed monotonic_clock to steady_clock to conform with C++11.
#if __GNUC__ == 4 && __GNUC_MINOR__ < 7
  static const bool is_monotonic = BaseClock::is_monotonic;
  static const bool is_steady = BaseClock::is_monotonic;
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
 * clock is desired.
 */
// libstdc++ 4.7 renamed monotonic_clock to steady_clock to conform with C++11.
// libstdc++ 4.8 seems to be the first version where std::chrono::steady_clock
// is usable with nanosecond granularity.
#if __GNUC__ == 4 && __GNUC_MINOR__ < 8
typedef MockableClock<CSteadyClock> SteadyClock;
#else
typedef MockableClock<std::chrono::steady_clock> SteadyClock;
#endif

/**
 * A clock that reads wall time and is affected by NTP adjustments.
 */
// libstdc++ 4.8 seems to be the first version where std::chrono::system_clock
// has nanosecond granularity.
#if __GNUC__ == 4 && __GNUC_MINOR__ < 8
typedef MockableClock<CSystemClock> SystemClock;
#else
typedef MockableClock<std::chrono::system_clock> SystemClock;
#endif

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

/**
 * Return the system time since the Unix epoch in nanoseconds.
 */
uint64_t getTimeNanos();

} // namespace LogCabin::Core::Time
} // namespace LogCabin::Core
} // namespace LogCabin

#endif /* LOGCABIN_CORE_TIME_H */
