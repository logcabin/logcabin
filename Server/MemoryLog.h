/* Copyright (c) 2012-2014 Stanford University
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

#include <cinttypes>
#include <deque>
#include <memory>
#include <string>

#include "Server/RaftLog.h"

#ifndef LOGCABIN_SERVER_MEMORYLOG_H
#define LOGCABIN_SERVER_MEMORYLOG_H

namespace LogCabin {
namespace Server {

// forward declaration
class Globals;

namespace RaftConsensusInternal {

class MemoryLog : public Log {
  public:
    MemoryLog();
    ~MemoryLog();

    /**
     * Append a new entry to the log.
     * \param entry
     *      Entry to place at the end of the log.
     * \return
     *      Object that can be used to wait for the entry to be durable.
     */
    std::unique_ptr<Sync> append(const Entry& entry);

    /**
     * Look up an entry by ID.
     * \param entryId
     *      Must be in the range [startIndex, getLastLogIndex()].
     * \return
     *      The entry corresponding to that entry ID. This reference is only
     *      guaranteed to be valid until the next time the log is modified.
     */
    const Entry& getEntry(uint64_t entryId) const;

    /**
     * Get the entry ID of the first entry in the log (whether or not this
     * entry exists).
     * \return
     *      1 for logs that have never had truncatePrefix called,
     *      otherwise the largest ID passed to truncatePrefix.
     *      See 'startIndex'.
     */
    uint64_t getLogStartIndex() const;

    /**
     * Get the entry ID of the most recent entry in the log.
     * \return
     *      The entry ID of the most recent entry in the log,
     *      or startIndex - 1 if the log is empty.
     */
    uint64_t getLastLogIndex() const;

    /**
     * Get the size of the entire log in bytes.
     */
    uint64_t getSizeBytes() const;

    /**
     * Delete the log entries before the given entry ID.
     * Once you truncate a prefix from the log, there's no way to undo this.
     * \param firstEntryId
     *      After this call, the log will contain no entries with ID less
     *      than firstEntryId. This can be any entry ID, including 0 and those
     *      past the end of the log.
     */
    void truncatePrefix(uint64_t firstEntryId);

    /**
     * Delete the log entries past the given entry ID.
     * This will not affect the log start ID.
     * \param lastEntryId
     *      After this call, the log will contain no entries with ID greater
     *      than lastEntryId. This can be any entry ID, including 0 and those
     *      past the end of the log.
     */
    void truncateSuffix(uint64_t lastEntryId);

    /**
     * Call this after changing #metadata.
     */
    void updateMetadata();

  protected:

    /**
     * The ID for the first entry in the log. Begins as 1 for new logs but
     * will be larger for logs that have been snapshotted.
     */
    uint64_t startId;

    /**
     * Stores the entries that make up the log.
     * The index into 'entries' is the ID of the entry minus 'startId'.
     * This is a deque rather than a vector to support fast prefix truncation
     * (used after snapshotting a prefix of the log).
     */
    std::deque<Entry> entries;
};

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_MEMORYLOG_H */

