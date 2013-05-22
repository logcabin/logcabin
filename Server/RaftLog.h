/* Copyright (c) 2012-2013 Stanford University
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
#include <string>

#include "build/Protocol/Raft.pb.h"
#include "build/Server/RaftLogMetadata.pb.h"

#ifndef LOGCABIN_SERVER_RAFTLOG_H
#define LOGCABIN_SERVER_RAFTLOG_H

namespace LogCabin {
namespace Server {

// forward declaration
class Globals;

namespace RaftConsensusInternal {

class Log {
  public:

    typedef Protocol::Raft::Entry Entry;

    Log();
    virtual ~Log();

    /**
     * Append a new entry to the log.
     * \param entry
     *      Its entryId is ignored; a new one is assigned and returned.
     * \return
     *      The newly appended entry's entryId.
     */
    virtual uint64_t append(const Entry& entry);

    /**
     * Look up an entry by ID.
     * \param entryId
     *      Must be in the range [startIndex, getLastLogIndex()].
     * \return
     *      The entry corresponding to that entry ID. This reference is only
     *      guaranteed to be valid until the next time the log is modified.
     */
    virtual const Entry& getEntry(uint64_t entryId) const;

    /**
     * Get the entry ID of the first entry in the log (whether or not this
     * entry exists).
     * \return
     *      1 for logs that have never had truncatePrefix called,
     *      otherwise the largest ID passed to truncatePrefix.
     *      See 'startIndex'.
     */
    virtual uint64_t getLogStartIndex() const;

    /**
     * Get the entry ID of the most recent entry in the log.
     * \return
     *      The entry ID of the most recent entry in the log,
     *      or 0 if the log is empty.
     */
    virtual uint64_t getLastLogIndex() const;

    /**
     * Delete the log entries before the given entry ID.
     * Once you truncate a prefix from the log, there's no way to undo this.
     * \param firstEntryId
     *      After this call, the log will contain no entries with ID less
     *      than firstEntryId. This can be any entry ID, including 0 and those
     *      past the end of the log.
     */
    virtual void truncatePrefix(uint64_t firstEntryId);

    /**
     * Delete the log entries past the given entry ID.
     * This will not affect the log start ID.
     * \param lastEntryId
     *      After this call, the log will contain no entries with ID greater
     *      than lastEntryId. This can be any entry ID, including 0 and those
     *      past the end of the log.
     */
    virtual void truncateSuffix(uint64_t lastEntryId);

    /**
     * Call this after changing #metadata.
     */
    virtual void updateMetadata();

    /**
     * Opaque metadata that the log keeps track of.
     */
    RaftLogMetadata::Metadata metadata;

    /**
     * Print out a Log for debugging purposes.
     */
    friend std::ostream& operator<<(std::ostream& os, const Log& log);

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

    // Log is not copyable
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
};

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_RAFTLOG_H */
