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

class Consensus {
  public:

    struct Entry {
        Entry();
        // TODO(ongaro): client serial number
        uint64_t entryId;
        bool hasData;
        std::string data;
    };

    Consensus();
    virtual ~Consensus();
    virtual void init() = 0;
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
     */
    virtual void
    snapshotDone(uint64_t lastIncludedIndex) = 0;

    uint64_t serverId;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_CONSENSUS_H */
