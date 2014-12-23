/* Copyright (c) 2014 Diego Ongaro
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

#include "Core/StringUtil.h"
#include "Core/Time.h"

namespace LogCabin {
namespace Core {
namespace {

using StringUtil::toString;

TEST(CoreTime, output_milliseconds) {
    EXPECT_EQ("5 ms", toString(std::chrono::milliseconds(5)));
}

TEST(CoreTime, output_nanoseconds) {
    EXPECT_EQ("5 ns", toString(std::chrono::nanoseconds(5)));
}

TEST(CoreTime, output_timepoint) {
    EXPECT_EQ("TimePoint::min()",
              toString(Time::SteadyClock::time_point::min()));
    EXPECT_EQ("TimePoint::max()",
              toString(Time::SystemClock::time_point::max()));
    EXPECT_LT(0.0, std::stold(toString(Time::SystemClock::now())));
}

TEST(CoreTime, makeTimeSpec) {
    struct timespec s;
    s = Time::makeTimeSpec(Time::SystemClock::time_point::max());
    EXPECT_EQ(9223372036, s.tv_sec);
    EXPECT_EQ(854775807, s.tv_nsec);
    s = Time::makeTimeSpec(Time::SystemClock::time_point::min());
    EXPECT_EQ(-9223372037, s.tv_sec);
    EXPECT_EQ(145224192, s.tv_nsec);
    s = Time::makeTimeSpec(Time::SystemClock::now());
    EXPECT_LT(1417720382U, s.tv_sec); // 2014-12-04
    EXPECT_GT(1893456000U, s.tv_sec); // 2030-01-01
    s = Time::makeTimeSpec(Time::SystemClock::time_point() +
                           std::chrono::nanoseconds(50));
    EXPECT_EQ(0, s.tv_sec);
    EXPECT_EQ(50, s.tv_nsec);
    s = Time::makeTimeSpec(Time::SystemClock::time_point() -
                           std::chrono::nanoseconds(50));
    EXPECT_EQ(-1, s.tv_sec);
    EXPECT_EQ(999999950, s.tv_nsec);
}

TEST(CoreTime, SystemClock_nanosecondGranularity) {
    std::chrono::nanoseconds::rep nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
           Time::SystemClock::now().time_since_epoch()).count();
    if (nanos % 1000 == 0) { // second try
        nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
           Time::SystemClock::now().time_since_epoch()).count();
    }
    EXPECT_LT(0, nanos % 1000);
}

TEST(CoreTime, CSystemClock_now_increasing) {
    Time::CSystemClock::time_point a = Time::CSystemClock::now();
    Time::CSystemClock::time_point b = Time::CSystemClock::now();
    EXPECT_LT(a, b);
}

TEST(CoreTime, CSystemClock_now_progressTimingSensistive) {
    Time::CSystemClock::time_point a = Time::CSystemClock::now();
    usleep(1000);
    Time::CSystemClock::time_point b = Time::CSystemClock::now();
    EXPECT_LT(a, b);
    EXPECT_LT(a + std::chrono::microseconds(500), b);
    EXPECT_LT(b, a + std::chrono::microseconds(1500));
}


TEST(CoreTime, SystemClock_now_increasing) {
    Time::SystemClock::time_point a = Time::SystemClock::now();
    Time::SystemClock::time_point b = Time::SystemClock::now();
    EXPECT_LT(a, b);
}

TEST(CoreTime, SystemClock_now_progressTimingSensistive) {
    Time::SystemClock::time_point a = Time::SystemClock::now();
    usleep(1000);
    Time::SystemClock::time_point b = Time::SystemClock::now();
    EXPECT_LT(a, b);
    EXPECT_LT(a + std::chrono::microseconds(500), b);
    EXPECT_LT(b, a + std::chrono::microseconds(1500));
}

TEST(CoreTime, SteadyClock_nanosecondGranularity) {
    std::chrono::nanoseconds::rep nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
           Time::SteadyClock::now().time_since_epoch()).count();
    if (nanos % 1000 == 0) { // second try
        nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
           Time::SteadyClock::now().time_since_epoch()).count();
    }
    EXPECT_LT(0, nanos % 1000);
}

TEST(CoreTime, CSteadyClock_now_increasing) {
    Time::CSteadyClock::time_point a = Time::CSteadyClock::now();
    Time::CSteadyClock::time_point b = Time::CSteadyClock::now();
    EXPECT_LT(a, b);
}

TEST(CoreTime, CSteadyClock_now_progressTimingSensistive) {
    Time::CSteadyClock::time_point a = Time::CSteadyClock::now();
    usleep(1000);
    Time::CSteadyClock::time_point b = Time::CSteadyClock::now();
    EXPECT_LT(a, b);
    EXPECT_LT(a + std::chrono::microseconds(500), b);
    EXPECT_LT(b, a + std::chrono::microseconds(1500));
}


TEST(CoreTime, SteadyClock_now_increasing) {
    Time::SteadyClock::time_point a = Time::SteadyClock::now();
    Time::SteadyClock::time_point b = Time::SteadyClock::now();
    EXPECT_LT(a, b);
}

TEST(CoreTime, SteadyClock_now_progressTimingSensistive) {
    Time::SteadyClock::time_point a = Time::SteadyClock::now();
    usleep(1000);
    Time::SteadyClock::time_point b = Time::SteadyClock::now();
    EXPECT_LT(a, b);
    EXPECT_LT(a + std::chrono::microseconds(500), b);
    EXPECT_LT(b, a + std::chrono::microseconds(1500));
}

TEST(CoreTime, rdtsc_increasing) {
    uint64_t a = Time::rdtsc();
    uint64_t b = Time::rdtsc();
    EXPECT_LT(a, b);
}

TEST(CoreTime, rdtsc_progressTimingSensitive) {
    uint64_t a = Time::rdtsc();
    usleep(1000);
    uint64_t b = Time::rdtsc();
    EXPECT_LT(a, b);
    EXPECT_LT(a + 1000 * 1000, b);
    EXPECT_LT(b, a + 10 * 1000 * 1000);
}

TEST(CoreTime, getTimeNanos) {
    EXPECT_LT(1417720382578042639U, Time::getTimeNanos()); // 2014-12-04
    EXPECT_GT(1893456000000000000U, Time::getTimeNanos()); // 2030-01-01
    uint64_t first = Time::getTimeNanos();
    uint64_t later = Time::getTimeNanos();
    EXPECT_LT(first, later);
}

} // namespace LogCabin::Core::<anonymous>
} // namespace LogCabin::Core
} // namespace LogCabin
