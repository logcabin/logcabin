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

#include <cassert>
#include <cmath>
#include <event2/util.h>
#include <limits>

#include "Core/Debug.h"
#include "Core/Random.h"
#include "Event/Internal.h"

namespace LogCabin {
namespace Core {
namespace Random {

namespace {

/**
 * Fill a variable of type T with some random bytes.
 */
template<typename T>
T
getRandomBytes()
{
    if (!LibEvent::initialized)
        PANIC("LibEvent not initialized");

    T buf;
    evutil_secure_rng_get_bytes(&buf, sizeof(buf));
    return buf;
}

/// Return a random number between 0 and 1.
double
randomUnit()
{
    return (double(random64()) /
            double(std::numeric_limits<uint64_t>::max()));
}

} // anonymous namespace

uint8_t
random8()
{
    return getRandomBytes<uint8_t>();
}

uint16_t
random16()
{
    return getRandomBytes<uint16_t>();
}

uint32_t
random32()
{
    return getRandomBytes<uint32_t>();
}

uint64_t
random64()
{
    return getRandomBytes<uint64_t>();
}

double
randomRangeDouble(double start, double end)
{
    return start + randomUnit()  * (end - start);
}

uint64_t
randomRange(uint64_t start, uint64_t end)
{
    return lround(randomRangeDouble(double(start), double(end)));
}

} // namespace LogCabin::Core::Random
} // namespace LogCabin::Core
} // namespace LogCabin
