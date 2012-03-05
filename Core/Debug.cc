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

#include <cstdarg>
#include <cstdio>
#include <ctime>

#include "Core/Debug.h"

namespace LogCabin {
namespace Core {
namespace Debug {


/**
 * String array to convert logLevels into a string that will be printed. This 
 * array must match the LogLevel enum in Core/Debug.h.
 */
static const char *logLevelToString[] =
{
    "ERROR",
    "WARNING",
    "NOTICE",
    "DEBUG",
    "VERBOSE",
};

void
log(const char* fileName, uint32_t lineNum, const char* func,
    enum LogLevel level, const char* format, ...)
{
    va_list ap;
    struct timespec now;

    if (level >= MAXLOGLEVEL)
        level = VERBOSE;

    clock_gettime(CLOCK_REALTIME, &now);
    fprintf(stderr, "%010lu.%09lu %s:%d in %s %s: ",
            now.tv_sec, now.tv_nsec,
            fileName, lineNum, func, logLevelToString[level]);

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fflush(stderr);
}

} // namespace LogCabin::Core::Debug
} // namespace LogCabin::Core
} // namespace LogCabin
