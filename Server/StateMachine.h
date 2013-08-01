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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "build/Protocol/Client.pb.h"
#include "Tree/Tree.h"

#ifndef LOGCABIN_SERVER_STATEMACHINE_H
#define LOGCABIN_SERVER_STATEMACHINE_H

namespace LogCabin {
namespace Server {

// forward declaration
class Consensus;

class StateMachine {
  public:
    explicit StateMachine(std::shared_ptr<Consensus> consensus);
    ~StateMachine();

    /**
     * \warning
     *      Be sure to wait() first!
     */
    bool getResponse(const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo,
                     Protocol::Client::CommandResponse& response) const;

    void wait(uint64_t entryId) const;

    void listLogs(const Protocol::Client::ListLogs::Request& request,
                  Protocol::Client::ListLogs::Response& response) const;

    void read(const Protocol::Client::Read::Request& request,
                  Protocol::Client::Read::Response& response) const;

    void getLastId(const Protocol::Client::GetLastId::Request& request,
                   Protocol::Client::GetLastId::Response& response) const;

    void readOnlyTreeRPC(
                const Protocol::Client::ReadOnlyTree::Request& request,
                Protocol::Client::ReadOnlyTree::Response& response) const;

  private:
    void threadMain();

    typedef Protocol::Client::Read::Response::OK::Entry Entry;
    typedef std::vector<Entry> Log;

    /**
     * Return true if the state machine should ignore the command (because it
     * is a duplicate of a previous command).
     */
    bool ignore(const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo) const;

    void advance(uint64_t entryId, const std::string& data);

    void openLog(const Protocol::Client::OpenLog::Request& request,
                 Protocol::Client::OpenLog::Response& response);
    void deleteLog(const Protocol::Client::DeleteLog::Request& request,
                   Protocol::Client::DeleteLog::Response& response);
    void append(const Protocol::Client::Append::Request& request,
                Protocol::Client::Append::Response& response);
    void readWriteTreeRPC(
                const Protocol::Client::ReadWriteTree::Request& request,
                Protocol::Client::ReadWriteTree::Response& response);
    void openSession(uint64_t entryId,
                     const Protocol::Client::OpenSession::Request& request);

    std::shared_ptr<Consensus> consensus;
    mutable std::mutex mutex;
    mutable std::condition_variable cond;
    uint64_t lastEntryId; // only written to by thread

    /**
     * Tracks state for a particular client.
     * Used to prevent duplicate processing of duplicate RPCs.
     */
    struct Session {
        Session()
            : firstOutstandingRPC(0)
            , responses()
        {
        }
        /**
         * Largest firstOutstandingRPC number processed from this client.
         * (RPCs that are ignored do not count for this purpose.)
         */
        uint64_t firstOutstandingRPC;
        /**
         * Maps from RPC numbers to responses.
         * Responses for RPCs numbered less that firstOutstandingRPC are
         * discarded from this map.
         */
        std::unordered_map<uint64_t, Protocol::Client::CommandResponse>
            responses;
    };

    /**
     * Client ID to Session map.
     * TODO(ongaro): Will need to clean up stale sessions somehow.
     */
    std::unordered_map<uint64_t, Session> sessions;

    /**
     * Look up a log by ID or throw LogDisappearedException.
     * Must be called holding #mutex.
     */
    std::vector<Entry>& getLog(uint64_t logId);

    uint64_t nextLogId;
    std::map<std::string, uint64_t> logNames;
    // This shared_ptr just exists to make std::vector<Entry> copyable.
    // This is a work-around for gcc 4.4, which can't handle move-only objects
    // in maps.
    std::unordered_map<uint64_t, std::shared_ptr<Log>> logs;

    /**
     * The hierarchical key-value store. Used in readOnlyTreeRPC and
     * readWriteTreeRPC.
     */
    Tree::Tree tree;

    /**
     * Repeatedly calls into the consensus module to get commands to process.
     */
    std::thread thread;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_STATEMACHINE_H
