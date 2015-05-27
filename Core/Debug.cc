/* Copyright (c) 2011-2012 Stanford University
 * Copyright (c) 2014-2015 Diego Ongaro
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
#include <unordered_map>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"
#include "include/LogCabin/Debug.h"

namespace LogCabin {
namespace Core {
namespace Debug {

DebugMessage::DebugMessage()
    : filename()
    , linenum()
    , function()
    , logLevel()
    , logLevelString()
    , processName()
    , threadName()
    , message()
{
}

DebugMessage::DebugMessage(const DebugMessage& other)
    : filename(other.filename)
    , linenum(other.linenum)
    , function(other.function)
    , logLevel(other.logLevel)
    , logLevelString(other.logLevelString)
    , processName(other.processName)
    , threadName(other.threadName)
    , message(other.message)
{
}

DebugMessage::DebugMessage(DebugMessage&& other)
    : filename(other.filename)
    , linenum(other.linenum)
    , function(other.function)
    , logLevel(other.logLevel)
    , logLevelString(other.logLevelString)
    , processName(std::move(other.processName))
    , threadName(std::move(other.threadName))
    , message(std::move(other.message))
{
}

DebugMessage::~DebugMessage()
{
}

DebugMessage&
DebugMessage::operator=(const DebugMessage& other)
{
    filename = other.filename;
    linenum = other.linenum;
    function = other.function;
    logLevel = other.logLevel;
    logLevelString = other.logLevelString;
    processName = processName;
    threadName = threadName;
    message = other.message;
    return *this;
}

DebugMessage&
DebugMessage::operator=(DebugMessage&& other)
{
    filename = other.filename;
    linenum = other.linenum;
    function = other.function;
    logLevel = other.logLevel;
    logLevelString = other.logLevelString;
    processName = std::move(other.processName);
    threadName = std::move(other.threadName);
    message = std::move(other.message);
    return *this;
}


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
 * Where log messages go (unless logHandler is set).
 */
FILE* stream = stderr;

/**
 * If set, a callback that takes log messages instead of the normal log file
 * (stream). This is exposed to client applications so they can integrate with
 * their own logging mechanism.
 */
std::function<void(DebugMessage)> logHandler;

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
size_t
calculateLengthFilePrefix()
{
    const char* start = __FILE__;
    const char* match = strstr(__FILE__, "Core/Debug.cc");
    assert(match != NULL);
    return size_t(match - start);
}

/// Stores result of calculateLengthFilePrefix().
const size_t lengthFilePrefix = calculateLengthFilePrefix();

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

FILE*
setLogFile(FILE* newFile)
{
    std::lock_guard<std::mutex> lockGuard(mutex);
    FILE* old = stream;
    stream = newFile;
    return old;
}

std::function<void(DebugMessage)>
setLogHandler(std::function<void(DebugMessage)> handler)
{
    std::lock_guard<std::mutex> lockGuard(mutex);
    std::function<void(DebugMessage)> old = logHandler;
    logHandler = handler;
    return old;
}

void
setLogPolicy(const std::vector<std::pair<std::string,
                                         std::string>>& newPolicy)
{
    std::lock_guard<std::mutex> lockGuard(mutex);
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
    std::lock_guard<std::mutex> lockGuard(mutex);
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

    if (logHandler) {
        DebugMessage d;
        d.filename = relativeFileName(fileName);
        d.linenum = int(lineNum);
        d.function = functionName;
        d.logLevel = int(level);
        d.logLevelString = logLevelToString[uint32_t(level)];
        d.processName = processName;
        d.threadName = ThreadId::getName();

        // this part is copied from Core::StringUtil::toString.
        va_start(ap, format);
        // We're not really sure how big of a buffer will be necessary.
        // Try 1K, if not the return value will tell us how much is necessary.
        size_t bufSize = 1024;
        while (true) {
            char buf[bufSize];
            // vsnprintf trashes the va_list, so copy it first
            va_list aq;
            va_copy(aq, ap);
            int r = vsnprintf(buf, bufSize, format, aq);
            va_end(aq);
            assert(r >= 0); // old glibc versions returned -1
            size_t r2 = size_t(r);
            if (r2 < bufSize) {
                buf[r2 - 1] = '\0'; // strip off "\n" added by LOG macro
                d.message = buf; // copy string
                break;
            }
            bufSize = size_t(r2) + 1;
        }
        va_end(ap);
        (logHandler)(d);
        return;
    }

    // Don't use Core::Time here since it could potentially call PANIC.
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // Failures are a little annoying here, since we can't exactly log
    // errors that come up.
    char formattedSeconds[64]; // a human-readable string now.tv_sec
    bool ok = false;
    { // First, try gmtime and strftime.
        struct tm calendarTime;
        if (gmtime_r(&now.tv_sec, &calendarTime) != NULL) {
            ok = (strftime(formattedSeconds,
                           sizeof(formattedSeconds),
                           "%F %T",
                           &calendarTime) > 0);
        }
    }
    if (!ok) { // If that failed, use the raw number.
        snprintf(formattedSeconds,
                 sizeof(formattedSeconds),
                 "%010lu",
                 now.tv_sec);
        formattedSeconds[sizeof(formattedSeconds) - 1] = '\0';
    }

    // This ensures that output on stderr won't be interspersed with other
    // output. This normally happens automatically for a single call to
    // fprintf, but must be explicit since we're using two calls here.
    flockfile(stream);

    fprintf(stream, "%s.%06lu %s:%d in %s() %s[%s:%s]: ",
            formattedSeconds, now.tv_nsec / 1000,
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
