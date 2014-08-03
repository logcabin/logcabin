/* Copyright (c) 2009-2014 Stanford University
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
 *
 *
 * Some of this code is copied from RAMCloud src/Common.cc _generateRandom(),
 * Copyright (c) 2009-2014 Stanford University also under the same ISC license.
 */

#include <cassert>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/Random.h"

namespace LogCabin {
namespace Core {
namespace Random {

namespace {

// Internal scratch state used by random_r 128 is the same size as
// initstate() uses for regular random(), see manpages for details.
// statebuf is malloc'ed and this memory is leaked, it could be a __thread
// buffer, but after running into linker issues with large thread local
// storage buffers, we thought better.
enum { STATE_BYTES = 128 };
__thread char* statebuf;
// random_r's state, must be handed to each call, and seems to refer to
// statebuf in some undocumented way.
__thread random_data randbuf;

/**
 * Initialize thread-local state for random number generator.
 * Must be called before any random numbers are generated.
 */
void
initRandom()
{
    if (statebuf == NULL) {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0)
            PANIC("Couldn't open /dev/urandom: %s", strerror(errno));
        unsigned int seed;
        ssize_t bytesRead = read(fd, &seed, sizeof(seed));
        close(fd);
        if (bytesRead != sizeof(seed))
            PANIC("Couldn't read full seed from /dev/urandom");
        statebuf = static_cast<char*>(malloc(STATE_BYTES));
        initstate_r(seed, statebuf, STATE_BYTES, &randbuf);
    }

    static_assert(RAND_MAX >= (1 << 31), "RAND_MAX too small");
}

/**
 * Fill a variable of type T with some random bytes.
 */
template<typename T>
T
getRandomBytes()
{
    T buf;
    size_t offset = 0;
    while (offset < sizeof(buf)) {
        uint64_t r = random64();
        size_t copy = std::min(sizeof(r), sizeof(buf) - offset);
        memcpy(reinterpret_cast<char*>(&buf) + offset, &r, copy);
        offset += copy;
    }
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
    // Each call to random returns 31 bits of randomness,
    // so we need three to get 64 bits of randomness.
    initRandom();
    int32_t lo, mid, hi;
    random_r(&randbuf, &lo);
    random_r(&randbuf, &mid);
    random_r(&randbuf, &hi);
    uint64_t r = (((uint64_t(hi) & 0x7FFFFFFF) << 33) | // NOLINT
                  ((uint64_t(mid) & 0x7FFFFFFF) << 2)  | // NOLINT
                  (uint64_t(lo) & 0x00000003)); // NOLINT
    return r;
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
