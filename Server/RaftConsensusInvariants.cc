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

#include "Core/Debug.h"
#include "Server/RaftConsensus.h"

namespace LogCabin {
namespace Server {
namespace RaftConsensusInternal {

#define expect(expr) do { \
    if (!(expr)) { \
        WARNING("`%s' is false", #expr); \
        ++errors; \
    } \
} while (0)

struct Invariants::ConsensusSnapshot {
    explicit ConsensusSnapshot(const RaftConsensus& consensus)
        : stateChangedCount(consensus.stateChanged.notificationCount)
        , exiting(consensus.exiting)
        , numPeerThreads(consensus.numPeerThreads)
        , lastLogIndex(consensus.log->getLastLogIndex())
        , lastLogTerm(consensus.log->getTerm(consensus.log->getLastLogIndex()))
        , configurationId(consensus.configuration->id)
        , configurationState(consensus.configuration->state)
        , currentTerm(consensus.currentTerm)
        , state(consensus.state)
        , commitIndex(consensus.commitIndex)
        , leaderId(consensus.leaderId)
        , votedFor(consensus.votedFor)
        , currentEpoch(consensus.currentEpoch)
        , startElectionAt(consensus.startElectionAt)
    {
    }

    uint64_t stateChangedCount;
    bool exiting;
    uint32_t numPeerThreads;
    uint64_t lastLogIndex;
    uint64_t lastLogTerm;
    uint64_t configurationId;
    Configuration::State configurationState;
    uint64_t currentTerm;
    RaftConsensus::State state;
    uint64_t commitIndex;
    uint64_t leaderId;
    uint64_t votedFor;
    uint64_t currentEpoch;
    TimePoint startElectionAt;
};


Invariants::Invariants(RaftConsensus& consensus)
    : consensus(consensus)
    , errors(0)
    , previous()
{
}

Invariants::~Invariants()
{
}

void
Invariants::checkAll()
{
    checkBasic();
    checkDelta();
    checkPeerBasic();
    checkPeerDelta();
}

void
Invariants::checkBasic()
{
    // Log terms monotonically increase
    uint64_t lastTerm = 0;
    for (uint64_t entryId = 1;
         entryId <= consensus.log->getLastLogIndex();
         ++entryId) {
        const Log::Entry& entry = consensus.log->getEntry(entryId);
        expect(entry.term() >= lastTerm);
        lastTerm = entry.term();
    }
    // The terms in the log do not exceed currentTerm
    expect(lastTerm <= consensus.currentTerm);

    // The current configuration should be the last one found in the log
    bool found = false;
    for (uint64_t entryId = consensus.log->getLastLogIndex();
         entryId > 0;
         --entryId) {
        const Log::Entry& entry = consensus.log->getEntry(entryId);
        if (entry.type() == Protocol::Raft::EntryType::CONFIGURATION) {
            expect(consensus.configuration->id == entryId);
            expect(consensus.configuration->state !=
                   Configuration::State::BLANK);
            found = true;
            break;
        }
    }
    if (!found) {
        expect(consensus.configuration->id == 0);
        expect(consensus.configuration->state == Configuration::State::BLANK);
    }

    // Servers with blank configurations should remain passive. Since the first
    // entry in every log is a configuration, they should also have empty logs.
    if (consensus.configuration->state == Configuration::State::BLANK) {
        expect(consensus.state == RaftConsensus::State::FOLLOWER);
        expect(consensus.log->getLastLogIndex() == 0);
    }

    // A server should not try to become candidate in a configuration it does
    // not belong to, and a server should not lead in a committed configuration
    // it does not belong to.
    if (!consensus.configuration->hasVote(
                                consensus.configuration->localServer)) {
        expect(consensus.state != RaftConsensus::State::CANDIDATE);
        if (consensus.configuration->id <= consensus.commitIndex)
            expect(consensus.state != RaftConsensus::State::LEADER);
    }

    // The commitIndex doesn't exceed the length of the log.
    expect(consensus.commitIndex <= consensus.log->getLastLogIndex());

    // advanceCommittedId is called everywhere it needs to be.
    if (consensus.state == RaftConsensus::State::LEADER) {
        uint64_t majorityEntry =
            consensus.configuration->quorumMin(&Server::getLastAgreeIndex);
        expect(consensus.log->getTerm(majorityEntry) != consensus.currentTerm
               || consensus.commitIndex >= majorityEntry);
    }

    // A leader always points its leaderId at itself.
    if (consensus.state == RaftConsensus::State::LEADER)
        expect(consensus.leaderId == consensus.serverId);

    // A leader always voted for itself. (Candidates can vote for others when
    // they abort an election.)
    if (consensus.state == RaftConsensus::State::LEADER) {
        expect(consensus.votedFor == consensus.serverId);
    }

    // A follower and candidate always has a timer set; a leader has it at
    // TimePoint::max().
    if (consensus.state == RaftConsensus::State::LEADER) {
        expect(consensus.startElectionAt == TimePoint::max());
    } else {
        expect(consensus.startElectionAt > TimePoint::min());
        expect(consensus.startElectionAt <=
               Clock::now() + std::chrono::milliseconds(
                                    RaftConsensus::ELECTION_TIMEOUT_MS * 2));
    }

    // Log metadata is updated when the term or vote changes.
    expect(consensus.log->metadata.current_term() == consensus.currentTerm);
    expect(consensus.log->metadata.voted_for() == consensus.votedFor);
}

void
Invariants::checkDelta()
{
    if (!previous) {
        previous.reset(new ConsensusSnapshot(consensus));
        return;
    }
    std::unique_ptr<ConsensusSnapshot> current(
                                        new ConsensusSnapshot(consensus));
    // Within a term, ...
    if (previous->currentTerm == current->currentTerm) {
        // the leader is set at most once.
        if (previous->leaderId != 0)
            expect(previous->leaderId == current->leaderId);
        // the vote is set at most once.
        if (previous->votedFor != 0)
            expect(previous->votedFor == current->votedFor);
        // a leader stays a leader.
        if (previous->state == RaftConsensus::State::LEADER)
            expect(current->state == RaftConsensus::State::LEADER);
    }

    // Once exiting is set, it doesn't get unset.
    if (previous->exiting)
        expect(current->exiting);

    // These variables monotonically increase.
    expect(previous->currentTerm <= current->currentTerm);
    expect(previous->commitIndex <= current->commitIndex);
    expect(previous->currentEpoch <= current->currentEpoch);

    // Change requires condition variable notification:
    if (previous->stateChangedCount == current->stateChangedCount) {
        expect(previous->currentTerm == current->currentTerm);
        expect(previous->state == current->state);
        expect(previous->lastLogIndex == current->lastLogIndex);
        expect(previous->lastLogTerm == current->lastLogTerm);
        expect(previous->commitIndex == current->commitIndex);
        expect(previous->exiting == current->exiting);
        expect(previous->numPeerThreads <= current->numPeerThreads);
        expect(previous->configurationId == current->configurationId);
        expect(previous->configurationState == current->configurationState);
        expect(previous->startElectionAt == current->startElectionAt);
     // TODO(ongaro):
     // an acknowledgement from a peer is received.
     // a server goes from not caught up to caught up.
    }

    previous = std::move(current);
}

void
Invariants::checkPeerBasic()
{
    for (auto it = consensus.configuration->knownServers.begin();
         it != consensus.configuration->knownServers.end();
         ++it) {
        Peer* peer = dynamic_cast<Peer*>(it->second.get()); // NOLINT
        if (peer == NULL)
            continue;
        if (consensus.exiting)
            expect(peer->exiting);
        if (!peer->requestVoteDone) {
            expect(!peer->haveVote_);
        }
        expect(peer->lastAgreeIndex <= consensus.log->getLastLogIndex());
        expect(peer->lastAckEpoch <= consensus.currentEpoch);
        expect(peer->nextHeartbeatTime <= Clock::now() +
               std::chrono::milliseconds(consensus.HEARTBEAT_PERIOD_MS));
        expect(peer->backoffUntil <= Clock::now() +
               std::chrono::milliseconds(consensus.RPC_FAILURE_BACKOFF_MS));

        // TODO(ongaro): anything about catchup?
    }
}

void
Invariants::checkPeerDelta()
{
    // TODO(ongaro): add checks
}

} // namespace RaftConsensusInternal

} // namespace LogCabin::Server
} // namespace LogCabin
