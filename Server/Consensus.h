/* Copyright (c) 2012 Stanford University
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
#include <stdexcept>
#include <string>

#ifndef LOGCABIN_SERVER_CONSENSUS_H
#define LOGCABIN_SERVER_CONSENSUS_H

namespace LogCabin {
namespace Server {

// forward declaration
namespace SnapshotFile {
class Reader;
class Writer;
}


// TODO(ongaro): move this
class ThreadInterruptedException : std::runtime_error {
  public:
    ThreadInterruptedException()
        : std::runtime_error("Thread was interrupted")
    {
    }
};

/**
 * This is the interface to the consensus module used by state machines.
 * Currently, the only implementation is RaftConsensus.
 */
class Consensus {
  public:

    /**
     * This is returned by getNextEntry().
     */
    struct Entry {
        /// Default constructor.
        Entry();
        /// Move constructor.
        Entry(Entry&& other);
        /// Destructor.
        ~Entry();

        /**
         * The entry ID for this entry (or the last one a snapshot covers).
         * Pass this as the lastEntryId argument to the next call to
         * getNextEntry().
         */
        uint64_t entryId;

        /**
         * The type of the entry.
         */
        enum {
            /**
             * This is a normal entry containing a client request for the state
             * machine. The 'data' field contains that request, and
             * 'snapshotReader' is not set.
             */
            DATA,
            /**
             * This is a snapshot: the state machine should clear its state and
             * load in the snapshot. The 'data' field is not set, and the
             * 'snapshotReader' should be used to read the snapshot contents
             * from.
             */
            SNAPSHOT,
            /**
             * Some entries should be ignored by the state machine (they are
             * consumed internally by the consensus module). For client service
             * threads to know when a state machine is up-to-date, it's easiest
             * for the state machine to get empty entries back for these, and
             * simply call back into getNextEntry() again with the next ID,
             * Entries of type 'SKIP' will have neither 'data' nor
             * 'snapshotReader' set.
             */
            SKIP,
        } type;

        /**
         * The client request for entries of type 'DATA'.
         */
        std::string data;

        /**
         * A handle to the snapshot file for entries of type 'SNAPSHOT'.
         */
        std::unique_ptr<SnapshotFile::Reader> snapshotReader;

        // copy and assign not allowed
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
    };

    /// Constructor.
    Consensus();

    /// Destructor.
    virtual ~Consensus();

    /// Initialize. Must be called before any other method.
    virtual void init() = 0;

    /// Signal the consensus module to exit (shut down threads, etc).
    virtual void exit() = 0;

    /**
     * This returns the entry following lastEntryId in the replicated log. Some
     * entries may be used internally by the consensus module. These will have
     * Entry.hasData set to false. The reason these are exposed to the state
     * machine is that the state machine waits to be caught up to the latest
     * committed entry ID in the replicated log; sometimes, but that entry
     * would otherwise never reach the state machine if it was for internal
     * use.
     * \throw ThreadInterruptedException
     */
    virtual Entry getNextEntry(uint64_t lastEntryId) const = 0;

    /**
     * Start taking a snapshot. Called by the state machine when it wants to
     * take a snapshot.
     * \param lastIncludedIndex
     *      The snapshot will cover log entries in the range
     *      [1, lastIncludedIndex].
     *      lastIncludedIndex must be committed (must have been previously
     *      returned by #getNextEntry()).
     * \return
     *      A file the state machine can dump its snapshot into.
     */
    virtual std::unique_ptr<SnapshotFile::Writer>
    beginSnapshot(uint64_t lastIncludedIndex) = 0;

    /**
     * Complete taking a snapshot for the log entries in range [1,
     * lastIncludedIndex]. Called by the state machine when it is done taking a
     * snapshot.
     * \param lastIncludedIndex
     *      The snapshot will cover log entries in the range
     *      [1, lastIncludedIndex].
     * \param writer
     *      A writer that has not yet been saved: the consensus module may
     *      have to discard the snapshot in case it's gotten a better snapshot
     *      from another server. If this snapshot is to be saved (normal case),
     *      the consensus module will call save() on it.
     */
    virtual void
    snapshotDone(uint64_t lastIncludedIndex,
                 std::unique_ptr<SnapshotFile::Writer> writer) = 0;

    /**
     * This server's unique ID. Not available until init() is called.
     */
    uint64_t serverId;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_CONSENSUS_H */
