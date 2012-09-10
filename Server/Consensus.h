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
#include <stdexcept>
#include <string>
#include <vector>

#ifndef LOGCABIN_SERVER_CONSENSUS_H
#define LOGCABIN_SERVER_CONSENSUS_H

namespace LogCabin {
namespace Server {

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

    uint64_t serverId;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_CONSENSUS_H */
