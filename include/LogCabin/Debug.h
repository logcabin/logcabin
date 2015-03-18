/* Copyright (c) 2010-2012 Stanford University
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

/**
 * \file
 * This file is used to control how LogCabin's debug log (event log) messages
 * are handled. When used by client applications, this should be accessed as
 * LogCabin::Client::Debug (the LogCabin::Core namespace is used internally in
 * LogCabin).
 */

#include <cstdio>
#include <initializer_list>
#include <functional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#ifndef LOGCABIN_INCLUDE_LOGCABIN_DEBUG_H
#define LOGCABIN_INCLUDE_LOGCABIN_DEBUG_H

namespace LogCabin {
namespace Core {
namespace Debug {

/**
 * When LogCabin wants to print a log message, this is the information that
 * gets included.
 */
struct DebugMessage {
    /// Default constructor.
    DebugMessage();
    /// Copy constructor.
    DebugMessage(const DebugMessage& other);
    /// Move constructor.
    DebugMessage(DebugMessage&& other);
    /// Destructor.
    ~DebugMessage();
    /// Copy assignment.
    DebugMessage& operator=(const DebugMessage& other);
    /// Move assignment.
    DebugMessage& operator=(DebugMessage&& other);

    /// The output of __FILE__.
    const char* filename;
    /// The output of __LINE__.
    int linenum;
    /// The output of __FUNCTION__.
    const char* function;
    /// The level of importance of the message as an integer.
    int logLevel;
    /// The level of importance of the message as a static string.
    const char* logLevelString;
    /// The name of the current process (its PID or server ID).
    std::string processName;
    /// The name of the current thread (by its function or its thread ID).
    std::string threadName;
    /// The contents of the message.
    std::string message;
};

/**
 * Change the file on which debug log messages are written.
 * Note that if a handler is set with setLogHandler, this file will not be
 * used.
 * \param newFile
 *      Handle to open file where log messages will be written.
 * \return
 *      Handle to previous log file (initialized to stderr on process start).
 */
FILE*
setLogFile(FILE* newFile);


/**
 * Accept log messages on the given callback instead of writing them to a file.
 * Call this again with an empty std::function() to clear it.
 * \param newHandler
 *      Callback invoked once per log message, possibly concurrently.
 * \return
 *      Previous callback (initialized to empty std::function on process start).
 */
std::function<void(DebugMessage)>
setLogHandler(std::function<void(DebugMessage)> newHandler);

/**
 * Specify the log messages that should be displayed for each filename.
 * This first component is a pattern; the second is a log level.
 * A filename is matched against each pattern in order: if the filename starts
 * with or ends with the pattern, the corresponding log level defines the most
 * verbose messages that are to be displayed for the file. If a filename
 * matches no pattern, its log level will default to NOTICE.
 */
void
setLogPolicy(const std::vector<
                        std::pair<std::string, std::string>>& newPolicy);
/**
 * See setLogPolicy.
 */
void
setLogPolicy(const std::initializer_list<
                        std::pair<std::string, std::string>>& newPolicy);


} // namespace LogCabin::Core::Debug
} // namespace LogCabin::Core
} // namespace LogCabin

#endif /* LOGCABIN_INCLUDE_LOGCABIN_DEBUG_H */
