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

#include <string>
#include <vector>

#include "RPC/Buffer.h"
#include "Storage/Types.h"

#ifndef LOGCABIN_STORAGE_LOGENTRY_H
#define LOGCABIN_STORAGE_LOGENTRY_H

namespace LogCabin {
namespace Storage {

/**
 * This is the representation of log entries used to read from and append to
 * Storage::Log instances.
 */
class LogEntry {
  public:
    /**
     * Construct an entry with user-supplied data and optional invalidations.
     */
    LogEntry(TimeStamp createTime,
             RPC::Buffer data,
             std::vector<EntryId> invalidations =
                std::vector<EntryId>());

    /**
     * Construct an entry that only has invalidations.
     */
    LogEntry(TimeStamp createTime,
             std::vector<EntryId> invalidations);

    /// Move constructor.
    LogEntry(LogEntry&& other);

    /// Move assignment.
    LogEntry& operator=(LogEntry&& other);

    /**
     * Return a string representation of this log entry.
     * This is useful for testing and debugging.
     * Non-printable data will be omitted from the output.
     */
    std::string toString() const;

    /**
     * The log this entry is a part of.
     */
    LogId logId;

    /**
     * The logId together with this field uniquely identifies the entry.
     */
    EntryId entryId;

    /**
     * The time this entry was created on the leader.
     */
    TimeStamp createTime;

    /**
     * A list of entry IDs that this entry invalidates.
     */
    std::vector<EntryId> invalidations;

    /**
     * If this is false, the user did not supply any data for the log entry
     * (that's different than #data having a length of 0).
     */
    bool hasData;

    /**
     * The user-supplied data for the log entry, if any.
     */
    RPC::Buffer data;

    // LogEntry is not copyable.
    LogEntry(const LogEntry&) = delete;
    LogEntry& operator=(const LogEntry&) = delete;
};

} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_LOGENTRY_H */
