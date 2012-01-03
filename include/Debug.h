/* Copyright (c) 2011 Stanford University
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

#include <stdint.h>

#ifndef DEBUG_H
#define DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * The log levels
 */
enum LogLevel {
    /// A fatal error has occurred.
    ERROR,
    /// A recoverable error has occurred.
    WARNING,
    /// A normal system message (useful for administrators and developers).
    NOTICE,
    /// A low-level debug message.
    DBG,
    /// Debug messages that are not usually useful.
    VERBOSE,
    /// Maximum log level.
    MAXLOGLEVEL,
};

void
Debug_Log(const char* fileName, uint32_t lineNum, const char* func,
          enum LogLevel level, const char* format, ...)
__attribute__((format(printf, 5, 6)));

/**
 * Print a log message.
 * \param[in] logLevel
 *      The log-level of this message.
 * \param[in] format
 *      A printf-style format string for the message.
 * \param[in] ...
 *      The arguments to the format string.
 */
#define LOG(logLevel, format, ...) do { \
    Debug_Log(__FILE__, __LINE__, __FUNCTION__, \
              logLevel, format "\n", ##__VA_ARGS__); \
} while (0)

/**
 * Log a warning message.
 * \param[in] format
 *      A printf-style format string for the message.
 * \param[in] ...
 *      The arguments to the format string.
 */
#define WARN(format, ...) \
    LOG(WARNING, format, ##__VA_ARGS__)

/**
 * Log an error message and coredump.
 * \param[in] format
 *      A printf-style format string for the message.
 * \param[in] ...
 *      The arguments to the format string.
 */
#define PANIC(format, ...) do { \
    LOG(ERROR, format, ##__VA_ARGS__); \
    abort(); \
} while (0)

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* DEBUG_H */

