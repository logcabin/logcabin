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
#include <memory>
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

/**
 * This interface is used by RaftConsensus to store log entries and metadata.
 * Typically, implementations will persist the log entries and metadata to
 * stable storage (but MemoryLog keeps it all in volatile memory).
 */
class Log {
  public:
    /**
     * This class provides an asynchronous interface
     * for flushing log entries to stable storage.
     * It's returned by Log::append().
     *
     * This class doesn't do anything but subclasses do override the wait()
     * method.
     */
    class Sync {
      public:
        Sync(uint64_t firstIndex, uint64_t lastIndex)
            : firstIndex(firstIndex)
            , lastIndex(lastIndex) {
        }
        virtual ~Sync() {}
        /**
         * Wait for the log entries to be durable.
         * It is safe to call this method on the same object from multiple
         * threads concurrently.
         * \return
         *      A string describing any errors that occurred, or the empty
         *      string.
         */
        virtual std::string wait() { return ""; }
        /**
         * The index of the first log entry that is being appended.
         */
        const uint64_t firstIndex;
        /**
         * The index of the last log entry that is being appended.
         */
        const uint64_t lastIndex;
    };

    /**
     * The type of a log entry (the same format that's used in AppendEntries).
     */
    typedef Protocol::Raft::Entry Entry;

    Log();
    virtual ~Log();

    /**
     * Append new entries to the log.
     * \param entries
     *      Entries to place at the end of the log.
     * \return
     *      Object that can be used to wait for the entries to be durable.
     */
    virtual std::unique_ptr<Sync> append(
                            const std::vector<const Entry*>& entries) = 0;

    /**
     * Append a single new entry to the log. This is a wrapper around the
     * vectored version of append; it's just for convenience. Callers that
     * have more than one entry to append should use the vectored form of
     * append directly for efficiency.
     * \param entry
     *      Entry to place at the end of the log.
     * \return
     *      Object that can be used to wait for the entry to be durable.
     */
    std::unique_ptr<Sync> appendSingle(const Entry& entry);

    /**
     * Look up an entry by its log index.
     * \param index
     *      Must be in the range [getLogStartIndex(), getLastLogIndex()].
     *      Otherwise, this will crash the server.
     * \return
     *      The entry corresponding to that index. This reference is only
     *      guaranteed to be valid until the next time the log is modified.
     */
    virtual const Entry& getEntry(uint64_t index) const = 0;

    /**
     * Get the index of the first entry in the log (whether or not this
     * entry exists).
     * \return
     *      1 for logs that have never had truncatePrefix called,
     *      otherwise the largest index passed to truncatePrefix.
     */
    virtual uint64_t getLogStartIndex() const = 0;

    /**
     * Get the index of the most recent entry in the log.
     * \return
     *      The index of the most recent entry in the log,
     *      or getLogStartIndex() - 1 if the log is empty.
     */
    virtual uint64_t getLastLogIndex() const = 0;

    /**
     * Get the size of the entire log in bytes.
     */
    virtual uint64_t getSizeBytes() const = 0;

    /**
     * Delete the log entries before the given index.
     * Once you truncate a prefix from the log, there's no way to undo this.
     * \param firstIndex
     *      After this call, the log will contain no entries indexed less
     *      than firstIndex. This can be any log index, including 0 and those
     *      past the end of the log.
     */
    virtual void truncatePrefix(uint64_t firstIndex) = 0;

    /**
     * Delete the log entries past the given index.
     * This will not affect the log start index.
     * \param lastIndex
     *      After this call, the log will contain no entries indexed greater
     *      than lastIndex. This can be any log index, including 0 and those
     *      past the end of the log.
     */
    virtual void truncateSuffix(uint64_t lastIndex) = 0;

    /**
     * Call this after changing #metadata.
     */
    virtual void updateMetadata() = 0;

    /**
     * Opaque metadata that the log keeps track of.
     */
    RaftLogMetadata::Metadata metadata;

    /**
     * Print out a Log for debugging purposes.
     */
    friend std::ostream& operator<<(std::ostream& os, const Log& log);

  protected:

    // Log is not copyable
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
};

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_RAFTLOG_H */
