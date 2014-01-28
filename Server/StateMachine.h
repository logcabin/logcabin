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

// TODO(ongaro): reorder file

// TODO(ongaro): document
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

    /**
     * Return once the state machine has applied at least the given entry.
     */
    void wait(uint64_t entryId) const;

    void readOnlyTreeRPC(
                const Protocol::Client::ReadOnlyTree::Request& request,
                Protocol::Client::ReadOnlyTree::Response& response) const;

  private:
    void applyThreadMain();
    void snapshotThreadMain();

    /**
     * Write the #sessions table to a snapshot file.
     */
    void dumpSessionSnapshot(
                google::protobuf::io::CodedOutputStream& stream) const;

    /**
     * Read the #sessions table from a snapshot file.
     */
    void loadSessionSnapshot(
                google::protobuf::io::CodedInputStream& stream);

    /**
     * Return true if it is time to create a new snapshot.
     * This is called by applyThread as an optimization to avoid waking up
     * snapshotThread upon applying every single entry.
     */
    bool shouldTakeSnapshot(uint64_t lastIncludedIndex) const;
    /**
     * Called by snapshotThreadMain to actually take the snapshot.
     */
    void takeSnapshot(uint64_t lastIncludedIndex,
                      std::unique_lock<std::mutex>& lockGuard);

    /**
     * Return true if the state machine should ignore the command (because it
     * is a duplicate of a previous command).
     */
    bool ignore(const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo) const;

    /**
     * Invoked once per committed entry from the Raft log.
     */
    void apply(uint64_t entryId, const std::string& data);

    void readWriteTreeRPC(
                const Protocol::Client::ReadWriteTree::Request& request,
                Protocol::Client::ReadWriteTree::Response& response);
    void openSession(uint64_t entryId,
                     const Protocol::Client::OpenSession::Request& request);


    std::shared_ptr<Consensus> consensus;

    /**
     * Protects against concurrent access for all members of this class (except
     * 'consensus', which is itself a monitor.
     */
    mutable std::mutex mutex;

    /**
     * Notified when lastEntryId changes after some entry got applied.
     * Also notified upon exiting.
     * This is used for client threads to wait; see wait().
     */
    mutable std::condition_variable entriesApplied;

    /**
     * Notified when shouldTakeSnapshot(lastEntryId) becomes true.
     * Also notified upon exiting.
     * This is used for snapshotThread to wake up only when necessary.
     */
    mutable std::condition_variable snapshotSuggested;

    /**
     * applyThread sets this to true to signal that the server is shutting
     * down.
     */
    bool exiting;

    /**
     * The PID of snapshotThread's child process, if any. This is used by
     * applyThread to signal exits: if applyThread is exiting, it sends SIGHUP
     * to this child process.
     */
    pid_t childPid;

    /**
     * The index of the last log entry that this state machine has applied.
     * This variable is only written to by applyThread, so applyThread is free
     * to access this variable without holding 'mutex'.
     */
    uint64_t lastEntryId;

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
     * The hierarchical key-value store. Used in readOnlyTreeRPC and
     * readWriteTreeRPC.
     */
    Tree::Tree tree;

    /**
     * Repeatedly calls into the consensus module to get commands to process
     * and applies them.
     */
    std::thread applyThread;

    /**
     * Takes snapshots with the help of a child process.
     */
    std::thread snapshotThread;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_STATEMACHINE_H
