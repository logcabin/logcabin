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

#include <gtest/gtest.h>

#include "Core/Random.h"

namespace LogCabin {
namespace Core {
namespace Random {
namespace {

TEST(CoreRandomTest, bitCoverage8) {
    uint8_t r = 0;
    for (uint32_t i = 0; i < 20; ++i)
       r |= random8();
    EXPECT_EQ(0xFF, r);
}

TEST(CoreRandomTest, bitCoverage16) {
    uint16_t r = 0;
    for (uint32_t i = 0; i < 20; ++i)
       r |= random16();
    EXPECT_EQ(0xFFFF, r);
}

TEST(CoreRandomTest, bitCoverage32) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < 20; ++i)
       r |= random32();
    EXPECT_EQ(~0U, r);
}

TEST(CoreRandomTest, bitCoverage64) {
    uint64_t r = 0;
    for (uint32_t i = 0; i < 20; ++i)
       r |= random64();
    EXPECT_EQ(~0UL, r);
}

} // namespace LogCabin::Core::Random::<anonymous>
} // namespace LogCabin::Core::Random
} // namespace LogCabin::Core
} // namespace LogCabin
