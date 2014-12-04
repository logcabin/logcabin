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

#include "Core/Time.h"

namespace LogCabin {
namespace Core {
namespace {

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
