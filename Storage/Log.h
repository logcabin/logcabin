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

#include <deque>

#include "Storage/Types.h"

#ifndef LOGCABIN_STORAGE_LOG_H
#define LOGCABIN_STORAGE_LOG_H

namespace LogCabin {
namespace Storage {

// forward declaration
class LogEntry;

/**
 * An interface that each storage module implements to read and manipulate an
 * individual log.
 */
class Log {
  public:

    /// Constructor.
    explicit Log(LogId logId)
        : logId(logId) {}

    /// Destructor.
    virtual ~Log() {}

    /**
     * Return the ID for the entry at the head of the log.
     * This will return NO_ENTRY_ID if the log is empty.
     */
    virtual EntryId getLastId() const = 0;

    /**
     * Return the entries from 'start' to the head of the log.
     * \param start
     *      The entry at which to start reading.
     * \return
     *      The entries starting at and including 'start' through head of the
     *      log.
     */
    virtual std::deque<const LogEntry*> readFrom(EntryId start) const = 0;

    /**
     * Append a log entry (data and/or invalidations).
     * \param entry
     *      The entry to append.
     * \return
     *      The ID of the newly appended entry.
     */
    virtual EntryId append(LogEntry entry) = 0;

    /**
     * A unique identifier for this log.
     */
    const LogId logId;

    // Log is not copyable
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
};


} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_LOG_H */
