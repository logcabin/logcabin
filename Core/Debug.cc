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

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"

namespace LogCabin {
namespace Core {
namespace Debug {

std::string processName = Core::StringUtil::format("%u", getpid());

namespace Internal {

/**
 * Used to convert LogLevels into strings that will be printed.
 * This array must match the LogLevel enum in Core/Debug.h.
 */
const char* logLevelToString[] =
{
    "SILENT",
    "ERROR",
    "WARNING",
    "NOTICE",
    "VERBOSE",
    NULL // must be the last element in the array
};

/**
 * Protects #policy and #isLoggingCache.
 */
std::mutex mutex;

/**
 * Specifies the log messages that should be displayed for each filename.
 * This first component is a pattern; the second is a log level.
 * A filename is matched against each pattern in order: if the filename starts
 * with or ends with the pattern, the corresponding log level defines the most
 * verbose messages that are to be displayed for the file. If a filename
 * matches no pattern, its log level will default to NOTICE.
 *
 * Protected by #mutex.
 */
std::vector<std::pair<std::string, std::string>> policy;

/**
 * A cache of the results of getLogLevel(), since that function is slow.
 * This needs to be cleared when the policy changes.
 * The key to the map is a pointer to the absolute filename, which should be a
 * string literal.
 *
 * Protected by #mutex.
 */
std::unordered_map<const char*, LogLevel> isLoggingCache;

/**
 * Where log messages go.
 */
FILE* stream = stderr;

/**
 * Convert a string to a log level.
 * PANICs if the string is not a valid log level (case insensitive).
 */
LogLevel
logLevelFromString(const std::string& level)
{
    for (uint32_t i = 0; logLevelToString[i] != NULL; ++i) {
        if (strcasecmp(logLevelToString[i], level.c_str()) == 0)
            return LogLevel(i);
    }
    log((LogLevel::ERROR), __FILE__, __LINE__, __FUNCTION__,
        "'%s' is not a valid log level.\n", level.c_str());
    abort();
}

/**
 * From the policy, calculate the most verbose log level that should be
 * displayed for this file.
 * This is slow, so isLogging() caches the results in isLoggingCache.
 *
 * Must be called with #mutex held.
 *
 * \param fileName
 *      Relative filename.
 */
LogLevel
getLogLevel(const char* fileName)
{
    for (auto it = policy.begin(); it != policy.end(); ++it) {
        const std::string& pattern = it->first;
        const std::string& logLevel = it->second;
        if (Core::StringUtil::startsWith(fileName, pattern) ||
            Core::StringUtil::endsWith(fileName, pattern)) {
            return logLevelFromString(logLevel);
        }
    }
    return LogLevel::NOTICE;
}

/**
 * Return the number of characters of __FILE__ that make up the path prefix.
 * That is, __FILE__ plus this value will be the relative path from the top
 * directory of the code tree.
 */
int
calculateLengthFilePrefix()
{
    const char* start = __FILE__;
    const char* match = strstr(__FILE__, "Core/Debug.cc");
    assert(match != NULL);
    return int(match - start);
}

/// Stores result of calculateLengthFilePrefix().
const int lengthFilePrefix = calculateLengthFilePrefix();

/**
 * Strip out the common prefix of a filename to get a path from the project's
 * root directory.
 * \param fileName
 *      An absolute or relative filename, usually the value of __FILE__.
 * \return
 *      A nicer way to show display 'fileName' to the user.
 *      For example, this file would yield "Core/Debug.cc".
 */
const char*
relativeFileName(const char* fileName)
{
    // Remove the prefix only if it matches that of __FILE__. This check is
    // needed in case someone compiles different files using different paths.
    if (strncmp(fileName, __FILE__, lengthFilePrefix) == 0)
        return fileName + lengthFilePrefix;
    else
        return fileName;
}

} // namespace Internal
using namespace Internal; // NOLINT

void
setLogPolicy(const std::vector<std::pair<std::string,
                                         std::string>>& newPolicy)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    policy = newPolicy;
    isLoggingCache.clear();
}

void
setLogPolicy(const std::initializer_list<std::pair<std::string,
                                         std::string>>& newPolicy)
{
    setLogPolicy(std::vector<std::pair<std::string,
                                         std::string>>(newPolicy));
}

std::ostream&
operator<<(std::ostream& ostream, LogLevel level)
{
    ostream << logLevelToString[uint32_t(level)];
    return ostream;
}

bool
isLogging(LogLevel level, const char* fileName)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    LogLevel verbosity;
    auto it = isLoggingCache.find(fileName);
    if (it == isLoggingCache.end()) {
        verbosity = getLogLevel(relativeFileName(fileName));
        isLoggingCache[fileName] = verbosity;
    } else {
        verbosity = it->second;
    }
    return uint32_t(level) <= uint32_t(verbosity);
}

void
log(LogLevel level,
    const char* fileName, uint32_t lineNum, const char* functionName,
    const char* format, ...)
{
    va_list ap;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // This ensures that output on stderr won't be interspersed with other
    // output. This normally happens automatically for a single call to
    // fprintf, but must be explicit since we're using two calls here.
    flockfile(stream);

    fprintf(stream, "%010lu.%06lu %s:%d in %s() %s[%s:%s]: ",
            now.tv_sec, now.tv_nsec / 1000,
            relativeFileName(fileName), lineNum, functionName,
            logLevelToString[uint32_t(level)],
            processName.c_str(), ThreadId::getName().c_str());

    va_start(ap, format);
    vfprintf(stream, format, ap);
    va_end(ap);

    funlockfile(stream);

    fflush(stream);
}

} // namespace LogCabin::Core::Debug
} // namespace LogCabin::Core
} // namespace LogCabin
