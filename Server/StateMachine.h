/* Copyright (c) 2012-2014 Stanford University
 * Copyright (c) 2015 Diego Ongaro
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

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "build/Protocol/Client.pb.h"
#include "build/Server/SnapshotStateMachine.pb.h"
#include "Core/ConditionVariable.h"
#include "Core/Config.h"
#include "Tree/Tree.h"

#ifndef LOGCABIN_SERVER_STATEMACHINE_H
#define LOGCABIN_SERVER_STATEMACHINE_H

namespace LogCabin {
namespace Server {

// forward declaration
class RaftConsensus;

/**
 * Interprets and executes operations that have been committed into the Raft
 * log.
 */
class StateMachine {
  public:
    enum {
        /**
         * This state machine code can behave like all versions between
         * MIN_SUPPORTED_VERSION and MAX_SUPPORTED_VERSION, inclusive.
         */
        MIN_SUPPORTED_VERSION = 1,
        /**
         * This state machine code can behave like all versions between
         * MIN_SUPPORTED_VERSION and MAX_SUPPORTED_VERSION, inclusive.
         */
        MAX_SUPPORTED_VERSION = 1,
    };


    StateMachine(std::shared_ptr<RaftConsensus> consensus,
                 Core::Config& config);
    ~StateMachine();

    /**
     * Called by ClientService to get a response for a read-write operation on
     * the Tree.
     * \warning
     *      Be sure to wait() first!
     * \param rpcInfo
     *      Identifies client session, etc.
     * \param[out] response
     *      If the return value is true, the response will be filled in here.
     *      Otherwise, this will be unmodified.
     * \return
     *      True if successful; false if the session expired.
     */
    bool getResponse(const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo,
                     Protocol::Client::CommandResponse& response) const;

    /**
     * Called by ClientService to execute read-only operations on the Tree.
     */
    void readOnlyTreeRPC(
                const Protocol::Client::ReadOnlyTree_Request& request,
                Protocol::Client::ReadOnlyTree_Response& response) const;

    /**
     * Add information about the state machine state to the given structure.
     */
    void updateServerStats(Protocol::ServerStats& serverStats) const;

    /**
     * Return once the state machine has applied at least the given entry.
     */
    void wait(uint64_t index) const;

  private:
    // forward declaration
    struct Session;

    /**
     * Invoked once per committed entry from the Raft log.
     */
    void apply(const RaftConsensus::Entry& entry);

    /**
     * Main function for thread that waits for new commands from Raft.
     */
    void applyThreadMain();

    /**
     * Return the #sessions table as a protobuf message for writing into a
     * snapshot.
     */
    void serializeSessions(SnapshotStateMachine::Header& header) const;

    /**
     * Update the session and clean up unnecessary responses.
     * \param session
     *      Affected session.
     * \param firstOutstandingRPC
     *      New value for the first outstanding RPC for a session.
     */
    void expireResponses(Session& session, uint64_t firstOutstandingRPC);

    /**
     * Remove old sessions.
     * \param clusterTime
     *      Sessions are kept if they have been modified during the last
     *      timeout period going backwards from the given time.
     */
    void expireSessions(uint64_t clusterTime);

    /**
     * Return the version of the state machine behavior as of the given log
     * index. Note that this is based on versionHistory internally, so if
     * you're changing that variable at the given index, update it first.
     */
    uint16_t getVersion(uint64_t logIndex) const;

    /**
     * If there is a current snapshot process, send it a SIGHUP and return
     * immediately.
     */
    void killSnapshotProcess(Core::HoldingMutex holdingMutex);

    /**
     * Restore the #sessions table from a snapshot.
     */
    void loadSessions(const SnapshotStateMachine::Header& header);

    /**
     * Read all of the state machine state from a snapshot file
     * (including version, sessions, and tree).
     */
    void loadSnapshot(Core::ProtoBuf::InputStream& stream);

    /**
     * Restore the #versionHistory table from a snapshot.
     */
    void loadVersionHistory(const SnapshotStateMachine::Header& header);

    /**
     * Return the #versionHistory table as a protobuf message for writing into
     * a snapshot.
     */
    void serializeVersionHistory(SnapshotStateMachine::Header& header) const;

    /**
     * Return true if it is time to create a new snapshot.
     * This is called by applyThread as an optimization to avoid waking up
     * snapshotThread upon applying every single entry.
     */
    bool shouldTakeSnapshot(uint64_t lastIncludedIndex) const;

    /**
     * Main function for thread that calls takeSnapshot when appropriate.
     */
    void snapshotThreadMain();

    /**
     * Main function for thread that checks the progress of the child process.
     */
    void snapshotWatchdogThreadMain();

    /**
     * Called by snapshotThreadMain to actually take the snapshot.
     */
    void takeSnapshot(uint64_t lastIncludedIndex,
                      std::unique_lock<std::mutex>& lockGuard);

    std::shared_ptr<RaftConsensus> consensus;

    /**
     * Used for testing the snapshot watchdog thread. The probability that a
     * snapshotting process will deadlock on purpose before starting, as a
     * percentage.
     */
    uint64_t snapshotBlockPercentage;

    /**
     * Size in bytes of smallest log to snapshot.
     */
    uint64_t snapshotMinLogSize;

    /**
     * Maximum log size as multiple of last snapshot size until server should
     * snapshot.
     */
    uint64_t snapshotRatio;

    /**
     * After this much time has elapsed without any progress, the snapshot
     * watchdog thread will kill the snapshotting process. A special value of 0
     * disables the watchdog entirely.
     */
    std::chrono::nanoseconds snapshotWatchdogInterval;

    /**
     * The time interval after which to remove an inactive client session, in
     * nanoseconds of cluster time.
     */
    uint64_t sessionTimeoutNanos;

    /**
     * Protects against concurrent access for all members of this class (except
     * 'consensus', which is itself a monitor.
     */
    mutable std::mutex mutex;

    /**
     * Notified when lastIndex changes after some entry got applied.
     * Also notified upon exiting.
     * This is used for client threads to wait; see wait().
     */
    mutable Core::ConditionVariable entriesApplied;

    /**
     * Notified when shouldTakeSnapshot(lastIndex) becomes true.
     * Also notified upon exiting.
     * This is used for snapshotThread to wake up only when necessary.
     */
    mutable Core::ConditionVariable snapshotSuggested;

    /**
     * Notified when a snapshot process is forked.
     * Also notified upon exiting.
     * This is used so that the watchdog thread knows to begin checking the
     * progress of the child process.
     */
    mutable Core::ConditionVariable snapshotStarted;

    /**
     * applyThread sets this to true to signal that the server is shutting
     * down.
     */
    bool exiting;

    /**
     * The PID of snapshotThread's child process, if any. This is used by
     * applyThread to signal exits: if applyThread is exiting, it sends SIGHUP
     * to this child process. A childPid of 0 indicates that there is no child
     * process.
     */
    pid_t childPid;

    /**
     * The index of the last log entry that this state machine has applied.
     * This variable is only written to by applyThread, so applyThread is free
     * to access this variable without holding 'mutex'. Other readers must hold
     * 'mutex'.
     */
    uint64_t lastIndex;

    /**
     * The number of times a snapshot has been started.
     * In addition to being a useful stat, the watchdog thread uses this to
     * know whether it's been watching the same snapshot or whether a new one
     * has been started.
     */
    uint64_t numSnapshotsAttempted;

    /**
     * The number of times a snapshot child process has failed to exit cleanly.
     */
    uint64_t numSnapshotsFailed;

    /**
     * The number of times a log entry was processed to advance the state
     * machine's running version, but the state machine was already at that
     * version.
     */
    uint64_t numRedundantAdvanceVersionEntries;

    /**
     * The number of times a log entry was processed to advance the state
     * machine's running version, but the state machine was already at a larger
     * version.
     */
    uint64_t numRejectedAdvanceVersionEntries;

    /**
     * The number of times a log entry was processed to successfully advance
     * the state machine's running version, where the state machine was
     * previously at a smaller version.
     */
    uint64_t numSuccessfulAdvanceVersionEntries;

    /**
     * The number of times any log entry to advance the state machine's running
     * version was processed. Should be the sum of redundant, rejected, and
     * successful counts.
     */
    uint64_t numTotalAdvanceVersionEntries;

    /**
     * Tracks state for a particular client.
     * Used to prevent duplicate processing of duplicate RPCs.
     */
    struct Session {
        Session()
            : lastModified(0)
            , firstOutstandingRPC(0)
            , responses()
        {
        }
        /**
         * When the session was last active, measured in cluster time
         * (roughly the number of nanoseconds that the cluster has maintained a
         * leader).
         */
        uint64_t lastModified;
        /**
         * Largest firstOutstandingRPC number processed from this client.
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
     */
    std::unordered_map<uint64_t, Session> sessions;

    /**
     * The hierarchical key-value store. Used in readOnlyTreeRPC and
     * readWriteTreeRPC.
     */
    Tree::Tree tree;

    /**
     * The log position when the state machine was updated to each new version.
     * First component: log index. Second component: version number.
     * Used to evolve state machine over time.
     *
     * This is used by getResponse() to determine the running version at a
     * given log index (to determine whether a command would have been
     * applied), and it's used elsewhere to determine the state machine's
     * current running version.
     *
     * Invariant: the pair (index 0, version 1) is always present.
     */
    std::map<uint64_t, uint16_t> versionHistory;

    /**
     * The file that the snapshot is being written into. Also used by to track
     * the progress of the child process for the watchdog thread.
     * This is non-empty if and only if childPid > 0.
     */
    std::unique_ptr<Storage::SnapshotFile::Writer> writer;

    /**
     * Repeatedly calls into the consensus module to get commands to process
     * and applies them.
     */
    std::thread applyThread;

    /**
     * Takes snapshots with the help of a child process.
     */
    std::thread snapshotThread;

    /**
     * Watches the child process to make sure it's writing to #writer, and
     * kills it otherwise. This is to detect any possible deadlock that might
     * occur if a thread in the parent at the time of the fork held a lock that
     * the child process then tried to access.
     * See https://github.com/logcabin/logcabin/issues/121 for more rationale.
     */
    std::thread snapshotWatchdogThread;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_STATEMACHINE_H
