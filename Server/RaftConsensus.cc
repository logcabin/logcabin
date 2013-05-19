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

#include <algorithm>
#include <string.h>
#include <time.h>

#include "build/Protocol/Raft.pb.h"
#include "build/Server/SnapshotMetadata.pb.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/Random.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"
#include "Core/Util.h"
#include "Protocol/Common.h"
#include "RPC/Buffer.h"
#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"
#include "RPC/ProtoBuf.h"
#include "RPC/ServerRPC.h"
#include "Server/RaftConsensus.h"
#include "Server/Globals.h"
#include "Server/SimpleFileLog.h"
#include "Server/SnapshotFile.h"
#include "Server/StateMachine.h"

namespace LogCabin {
namespace Server {

namespace RaftConsensusInternal {

bool startThreads = true;

////////// Server //////////

Server::Server(uint64_t serverId)
    : serverId(serverId)
    , address()
    , gcFlag(false)
{
}

Server::~Server()
{
}

std::ostream&
operator<<(std::ostream& os, const Server& server)
{
    return server.dumpToStream(os);
}

////////// LocalServer //////////

LocalServer::LocalServer(uint64_t serverId, RaftConsensus& consensus)
    : Server(serverId)
    , consensus(consensus)
{
}

LocalServer::~LocalServer()
{
}

void
LocalServer::beginRequestVote()
{
}

void
LocalServer::beginLeadership()
{
}

void
LocalServer::exit()
{
}

uint64_t
LocalServer::getLastAckEpoch() const
{
    return consensus.currentEpoch;
}

uint64_t
LocalServer::getLastAgreeIndex() const
{
    return consensus.log->getLastLogIndex();
}

bool
LocalServer::haveVote() const
{
    return (consensus.votedFor == serverId);
}

void
LocalServer::interrupt()
{
}

bool
LocalServer::isCaughtUp() const
{
    return true;
}

void
LocalServer::scheduleHeartbeat()
{
}

std::ostream&
LocalServer::dumpToStream(std::ostream& os) const
{
    // Nothing interesting to dump.
    return os;
}

////////// Peer //////////

Peer::Peer(uint64_t serverId, RaftConsensus& consensus)
    : Server(serverId)
    , consensus(consensus)
    , eventLoop(consensus.globals.eventLoop)
    , exiting(false)
    , requestVoteDone(false)
    , haveVote_(false)
    , forceHeartbeat(true)
      // It's somewhat important to set nextIndex correctly here, since peers
      // that are added to the configuration won't go through beginLeadership()
      // on the current leader. I say somewhat important because, if nextIndex
      // is set incorrectly, it's self-correcting, so it's just a potential
      // performance issue.
    , nextIndex(consensus.log->getLastLogIndex() + 1)
    , lastAgreeIndex(0)
    , lastAckEpoch(0)
    , nextHeartbeatTime(TimePoint::min())
    , backoffUntil(TimePoint::min())
    , lastCatchUpIterationMs(~0UL)
    , thisCatchUpIterationStart(Clock::now())
    , thisCatchUpIterationGoalId(~0UL)
    , isCaughtUp_(false)
    , session()
    , rpc()
    , thread()
{
}

Peer::~Peer()
{
}

void
Peer::beginRequestVote()
{
    requestVoteDone = false;
    haveVote_ = false;
}

void
Peer::beginLeadership()
{
    nextIndex = consensus.log->getLastLogIndex() + 1;
    lastAgreeIndex = 0;
    forceHeartbeat = true;
}

void
Peer::exit()
{
    exiting = true;
}

uint64_t
Peer::getLastAckEpoch() const
{
    return lastAckEpoch;
}

uint64_t
Peer::getLastAgreeIndex() const
{
    return lastAgreeIndex;
}

bool
Peer::haveVote() const
{
    return haveVote_;
}

void
Peer::interrupt()
{
    rpc.cancel();
}

bool
Peer::isCaughtUp() const
{
    return isCaughtUp_;
}

void
Peer::scheduleHeartbeat()
{
    nextHeartbeatTime = Clock::now();
}

bool
Peer::callRPC(Protocol::Raft::OpCode opCode,
              const google::protobuf::Message& request,
              google::protobuf::Message& response,
              std::unique_lock<Mutex>& lockGuard)
{
    typedef RPC::ClientRPC::Status RPCStatus;
    rpc = RPC::ClientRPC(getSession(lockGuard),
                         Protocol::Common::ServiceId::RAFT_SERVICE,
                         /* serviceSpecificErrorVersion = */ 0,
                         opCode,
                         request);
    // release lock for concurrency
    Core::MutexUnlock<Mutex> unlockGuard(lockGuard);
    switch (rpc.waitForReply(&response, NULL)) {
        case RPCStatus::OK:
            return true;
        case RPCStatus::SERVICE_SPECIFIC_ERROR:
            PANIC("unexpected service-specific error");
        default:
            WARNING("RPC to server failed: %s",
                    rpc.getErrorMessage().c_str());
            return false;
    }
}

void
Peer::startThread(std::shared_ptr<Peer> self)
{
    thisCatchUpIterationStart = Clock::now();
    thisCatchUpIterationGoalId = consensus.log->getLastLogIndex();
    ++consensus.numPeerThreads;
    thread = std::thread(&RaftConsensus::peerThreadMain, &consensus, self);
    thread.detach();
}

std::shared_ptr<RPC::ClientSession>
Peer::getSession(std::unique_lock<Mutex>& lockGuard)
{
    if (!session || !session->getErrorMessage().empty()) {
        // release lock for concurrency
        Core::MutexUnlock<Mutex> unlockGuard(lockGuard);
        session = RPC::ClientSession::makeSession(
            eventLoop,
            RPC::Address(address, Protocol::Common::DEFAULT_PORT),
            Protocol::Common::MAX_MESSAGE_LENGTH);
    }
    return session;
}

std::ostream&
Peer::dumpToStream(std::ostream& os) const
{
    os << "Peer " << serverId << std::endl;
    os << "address: " << address << std::endl;
    switch (consensus.state) {
        case RaftConsensus::State::FOLLOWER:
            break;
        case RaftConsensus::State::CANDIDATE:
            os << "vote: ";
            if (requestVoteDone) {
                if (haveVote_)
                    os << "granted";
                else
                    os << "not granted";
            } else {
                os << "no response";
            }
            os << std::endl;
            break;
        case RaftConsensus::State::LEADER:
            os << "forceHeartbeat: " << forceHeartbeat << std::endl;
            os << "nextIndex: " << nextIndex << std::endl;
            os << "lastAgreeIndex: " << lastAgreeIndex << std::endl;
            break;
    }
    os << "address: " << address << std::endl;
    return os;
}

////////// Configuration::SimpleConfiguration //////////

Configuration::SimpleConfiguration::SimpleConfiguration()
    : servers()
{
}

Configuration::SimpleConfiguration::~SimpleConfiguration()
{
}

bool
Configuration::SimpleConfiguration::all(const Predicate& predicate) const
{
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        if (!predicate(**it))
            return false;
    }
    return true;
}

bool
Configuration::SimpleConfiguration::contains(std::shared_ptr<Server> server)
                                                                          const
{
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        if (*it == server)
            return true;
    }
    return false;
}

void
Configuration::SimpleConfiguration::forEach(const SideEffect& sideEffect)
{
    for (auto it = servers.begin(); it != servers.end(); ++it)
        sideEffect(**it);
}

uint64_t
Configuration::SimpleConfiguration::min(const GetValue& getValue) const
{
    if (servers.empty())
        return 0;
    uint64_t smallest = ~0UL;
    for (auto it = servers.begin(); it != servers.end(); ++it)
        smallest = std::min(smallest, getValue(**it));
    return smallest;
}

bool
Configuration::SimpleConfiguration::quorumAll(const Predicate& predicate) const
{
    if (servers.empty())
        return true;
    uint64_t count = 0;
    for (auto it = servers.begin(); it != servers.end(); ++it)
        if (predicate(**it))
            ++count;
    return (count >= servers.size() / 2 + 1);
}

uint64_t
Configuration::SimpleConfiguration::quorumMin(const GetValue& getValue) const
{
    if (servers.empty())
        return 0;
    std::vector<uint64_t> values;
    for (auto it = servers.begin(); it != servers.end(); ++it)
        values.push_back(getValue(**it));
    std::sort(values.begin(), values.end());
    return values.at((values.size() - 1)/ 2);
}

////////// Configuration //////////

Configuration::Configuration(uint64_t serverId, RaftConsensus& consensus)
    : consensus(consensus)
    , knownServers()
    , localServer()
    , state(State::BLANK)
    , id(0)
    , description()
    , oldServers()
    , newServers()
{
    localServer.reset(new LocalServer(serverId, consensus));
    knownServers[serverId] = localServer;
}

Configuration::~Configuration()
{
}

void
Configuration::forEach(const SideEffect& sideEffect)
{
    for (auto it = knownServers.begin(); it != knownServers.end(); ++it)
        sideEffect(*it->second);
}

bool
Configuration::hasVote(std::shared_ptr<Server> server) const
{
    if (state == State::TRANSITIONAL) {
        return (oldServers.contains(server) ||
                newServers.contains(server));
    } else {
        return oldServers.contains(server);
    }
}

bool
Configuration::quorumAll(const Predicate& predicate) const
{
    if (state == State::TRANSITIONAL) {
        return (oldServers.quorumAll(predicate) &&
                newServers.quorumAll(predicate));
    } else {
        return oldServers.quorumAll(predicate);
    }
}

uint64_t
Configuration::quorumMin(const GetValue& getValue) const
{
    if (state == State::TRANSITIONAL) {
        return std::min(oldServers.quorumMin(getValue),
                        newServers.quorumMin(getValue));
    } else {
        return oldServers.quorumMin(getValue);
    }
}

void
Configuration::resetStagingServers()
{
    if (state == State::STAGING) {
        // staging servers could have changed other servers' addresses, so roll
        // back to old description with old addresses
        setConfiguration(id, description);
    }
}

namespace {
void setGCFlag(Server& server)
{
    server.gcFlag = true;
}
} // anonymous namespace

void
Configuration::setConfiguration(
        uint64_t newId,
        const Protocol::Raft::Configuration& newDescription)
{
    NOTICE("Activating configuration %lu:\n%s", newId,
           Core::ProtoBuf::dumpString(newDescription).c_str());

    if (newDescription.next_configuration().servers().size() == 0)
        state = State::STABLE;
    else
        state = State::TRANSITIONAL;
    id = newId;
    description = newDescription;
    oldServers.servers.clear();
    newServers.servers.clear();

    // Build up the list of old servers
    for (auto confIt = description.prev_configuration().servers().begin();
         confIt != description.prev_configuration().servers().end();
         ++confIt) {
        std::shared_ptr<Server> server = getServer(confIt->server_id());
        server->address = confIt->address();
        oldServers.servers.push_back(server);
    }

    // Build up the list of new servers
    for (auto confIt = description.next_configuration().servers().begin();
         confIt != description.next_configuration().servers().end();
         ++confIt) {
        std::shared_ptr<Server> server = getServer(confIt->server_id());
        server->address = confIt->address();
        newServers.servers.push_back(server);
    }

    // Servers not in the current configuration need to be told to exit
    setGCFlag(*localServer);
    oldServers.forEach(setGCFlag);
    newServers.forEach(setGCFlag);
    auto it = knownServers.begin();
    while (it != knownServers.end()) {
        std::shared_ptr<Server> server = it->second;
        if (!server->gcFlag) {
            server->exit();
            it = knownServers.erase(it);
        } else {
            server->gcFlag = false; // clear flag for next time
            ++it;
        }
    }
}

void
Configuration::setStagingServers(
        const Protocol::Raft::SimpleConfiguration& stagingServers)
{
    assert(state == State::STABLE);
    state = State::STAGING;
    for (auto it = stagingServers.servers().begin();
         it != stagingServers.servers().end();
         ++it) {
        std::shared_ptr<Server> server = getServer(it->server_id());
        server->address = it->address();
        newServers.servers.push_back(server);
    }
}

bool
Configuration::stagingAll(const Predicate& predicate) const
{
    if (state == State::STAGING)
        return newServers.all(predicate);
    else
        return true;
}

uint64_t
Configuration::stagingMin(const GetValue& getValue) const
{
    if (state == State::STAGING)
        return newServers.min(getValue);
    else
        return 0;
}

std::ostream&
operator<<(std::ostream& os, Configuration::State state)
{
    typedef Configuration::State State;
    switch (state) {
        case State::BLANK:
            os << "State::BLANK";
            break;
        case State::STABLE:
            os << "State::STABLE";
            break;
        case State::STAGING:
            os << "State::STAGING";
            break;
        case State::TRANSITIONAL:
            os << "State::TRANSITIONAL";
            break;
    }
    return os;
}

std::ostream&
operator<<(std::ostream& os, const Configuration& configuration)
{
    os << "Configuration: {" << std::endl;
    os << "  state: " << configuration.state << std::endl;
    os << "  id: " << configuration.id << std::endl;
    os << "  description: " << std::endl;
    os << Core::ProtoBuf::dumpString(configuration.description);
    os << "}" << std::endl;
    for (auto it = configuration.knownServers.begin();
         it != configuration.knownServers.end();
         ++it) {
        os << *it->second;
    }
    return os;
}


////////// Configuration private methods //////////

std::shared_ptr<Server>
Configuration::getServer(uint64_t newServerId)
{
    auto it = knownServers.find(newServerId);
    if (it != knownServers.end()) {
        return it->second;
    } else {
        std::shared_ptr<Peer> peer(new Peer(newServerId, consensus));
        if (startThreads)
            peer->startThread(peer);
        knownServers[newServerId] = peer;
        return peer;
    }
}

////////// RaftConsensus //////////

uint64_t RaftConsensus::ELECTION_TIMEOUT_MS = 150;

uint64_t RaftConsensus::HEARTBEAT_PERIOD_MS = ELECTION_TIMEOUT_MS / 2;

// this is just set high for now so log messages shut up
uint64_t RaftConsensus::RPC_FAILURE_BACKOFF_MS = 2000;

uint64_t RaftConsensus::SOFT_RPC_SIZE_LIMIT =
                Protocol::Common::MAX_MESSAGE_LENGTH - 1024;

RaftConsensus::RaftConsensus(Globals& globals)
    : globals(globals)
    , mutex()
    , stateChanged()
    , exiting(false)
    , numPeerThreads(0)
    , log()
    , configurationDescriptions()
    , configuration()
    , currentTerm(0)
    , state(State::FOLLOWER)
    , lastSnapshotIndex(0)
    , snapshotReader()
    , commitIndex(0)
    , leaderId(0)
    , votedFor(0)
    , currentEpoch(0)
    , startElectionAt(TimePoint::max())
    , timerThread()
    , stepDownThread()
    , invariants(*this)
{
}

RaftConsensus::~RaftConsensus()
{
    exit();
    if (timerThread.joinable())
        timerThread.join();
    if (stepDownThread.joinable())
        stepDownThread.join();
    std::unique_lock<Mutex> lockGuard(mutex);
    while (numPeerThreads > 0)
        stateChanged.wait(lockGuard);
}

void
RaftConsensus::init()
{
    std::unique_lock<Mutex> lockGuard(mutex);
    mutex.callback = std::bind(&Invariants::checkAll, &invariants);
    NOTICE("My server ID is %lu", serverId);

    if (!log) { // some unit tests pre-set the log; don't overwrite it
        // TODO(ongaro): use configuration option instead of hard-coded string
        log.reset(new SimpleFileLog(
                        Core::StringUtil::format("log/%lu", serverId)));
    }
    for (uint64_t entryId = log->getLogStartIndex();
         entryId <= log->getLastLogIndex();
         ++entryId) {
        const Log::Entry& entry = log->getEntry(entryId);
        if (entry.type() == Protocol::Raft::EntryType::CONFIGURATION)
            configurationDescriptions[entryId] = entry.configuration();
    }

    NOTICE("The log contains indexes %lu through %lu (inclusive)",
           log->getLogStartIndex(), log->getLastLogIndex());
    readSnapshot();

    if (log->metadata.has_current_term())
        currentTerm = log->metadata.current_term();
    if (log->metadata.has_voted_for())
        votedFor = log->metadata.voted_for();
    updateLogMetadata();

    // apply last configuration found
    configuration.reset(new Configuration(serverId, *this));
    auto it = configurationDescriptions.rbegin();
    if (it != configurationDescriptions.rend())
        configuration->setConfiguration(it->first, it->second);
    else
        NOTICE("No configuration, waiting to receive one");

    stepDown(currentTerm);
    if (startThreads) {
        timerThread = std::thread(&RaftConsensus::timerThreadMain,
                                      this);
        stepDownThread = std::thread(&RaftConsensus::stepDownThreadMain,
                                     this);
    }
    // log->path = ""; // hack to disable disk
    stateChanged.notify_all();
}

void
RaftConsensus::exit()
{
    std::unique_lock<Mutex> lockGuard(mutex);
    exiting = true;
    if (configuration)
        configuration->forEach(&Server::exit);
    interruptAll();
}

RaftConsensus::ClientResult
RaftConsensus::getConfiguration(
        Protocol::Raft::SimpleConfiguration& currentConfiguration,
        uint64_t& id) const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    if (!upToDateLeader(lockGuard))
        return ClientResult::NOT_LEADER;
    if (configuration->state != Configuration::State::STABLE ||
        commitIndex < configuration->id) {
        return ClientResult::RETRY;
    }
    currentConfiguration = configuration->description.prev_configuration();
    id = configuration->id;
    return ClientResult::SUCCESS;
}

std::pair<RaftConsensus::ClientResult, uint64_t>
RaftConsensus::getLastCommittedId() const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    if (!upToDateLeader(lockGuard))
        return {ClientResult::NOT_LEADER, 0};
    else
        return {ClientResult::SUCCESS, commitIndex};
}

Consensus::Entry
RaftConsensus::getNextEntry(uint64_t lastEntryId) const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    uint64_t nextEntryId = lastEntryId + 1;
    while (true) {
        if (exiting)
            throw ThreadInterruptedException();
        if (commitIndex >= nextEntryId) {
            Consensus::Entry entry;

            // Make the state machine load a snapshot if we don't have the next
            // entry it needs in the log.
            if (log->getLogStartIndex() > nextEntryId) {
                entry.type = Consensus::Entry::SNAPSHOT;
                // For well-behaved state machines, we expect 'snapshotReader'
                // to contain a SnapshotFile::Reader that we can return
                // directly to the state machine. In the case that a State
                // Machine asks for the snapshot again, we have to build a new
                // SnapshotFile::Reader again.
                entry.snapshotReader = std::move(snapshotReader);
                if (!entry.snapshotReader) {
                    WARNING("State machine asked for same snapshot twice; "
                            "this shouldn't happen in normal operation. "
                            "Having to re-read it from disk.");
                    // readSnapshot() shouldn't have any side effects since the
                    // snapshot should have already been read, so const_cast
                    // should be ok (though ugly).
                    const_cast<RaftConsensus*>(this)->readSnapshot();
                    entry.snapshotReader = std::move(snapshotReader);
                }
                entry.entryId = lastSnapshotIndex;
            } else {
                // not a snapshot
                const Log::Entry& logEntry = log->getEntry(nextEntryId);
                entry.entryId = nextEntryId;
                if (logEntry.type() == Protocol::Raft::EntryType::DATA) {
                    entry.type = Consensus::Entry::DATA;
                    entry.data = logEntry.data();
                } else {
                    entry.type = Consensus::Entry::SKIP;
                }
            }
            return entry;
        }
        stateChanged.wait(lockGuard);
    }
}

void
RaftConsensus::handleAppendEntries(
                    const Protocol::Raft::AppendEntries::Request& request,
                    Protocol::Raft::AppendEntries::Response& response)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    assert(!exiting);

    // Set response to a rejection. We'll overwrite these later if we end up
    // accepting the request.
    response.set_term(currentTerm);
    response.set_success(false);

    // If the caller's term is stale, just return our term to it.
    if (request.term() < currentTerm) {
        VERBOSE("Caller(%lu) is stale. Our term is %lu, theirs is %lu",
                 request.server_id(), currentTerm, request.term());
        return; // response was set to a rejection above
    }
    if (request.term() > currentTerm) {
        VERBOSE("Caller(%lu) has newer term, updating. "
                "Ours was %lu, theirs is %lu",
                request.server_id(), currentTerm, request.term());
        // We're about to bump our term in the stepDown below: update
        // 'response' accordingly.
        response.set_term(request.term());
    }
    // This request is a sign of life from the current leader. Update our term
    // and convert to follower if necessary; reset the election timer.
    stepDown(request.term());
    setElectionTimer();

    // Record the leader ID as a hint for clients.
    if (leaderId == 0) {
        leaderId = request.server_id();
        NOTICE("All hail leader %lu for term %lu", leaderId, currentTerm);
    } else {
        assert(leaderId == request.server_id());
    }

    // For an entry to fit into our log, it must not leave a gap.
    if (request.prev_log_index() > log->getLastLogIndex()) {
        VERBOSE("Rejecting AppendEntries RPC: would leave gap");
        return; // response was set to a rejection above
    }
    // It must also agree with the previous entry in the log (and, inductively
    // all prior entries). We could truncate the log here, but there's no real
    // advantage to doing that.
    if (log->getTerm(request.prev_log_index()) != request.prev_log_term()) {
        VERBOSE("Rejecting AppendEntries RPC: terms don't agree");
        return; // response was set to a rejection above
    }

    // If we got this far, we're accepting the request.
    response.set_success(true);

    // This needs to be able to handle duplicated RPC requests. We compare the
    // entries' terms to know if we need to do the operation; otherwise,
    // reapplying requests can result in data loss.
    //
    // The first problem this solves is that an old AppendEntries request may be
    // duplicated and received after a newer request, which could cause
    // undesirable data loss. For example, suppose the leader appends entry 4
    // and then entry 5, but the follower receives 4, then 5, then 4 again.
    // Without this extra guard, the follower would truncate 5 out of its
    // log.
    //
    // The second problem is more subtle: if the same request is duplicated but
    // the leader processes an earlier response, it will assume the
    // acknowledged data is safe. However, there is a window of vulnerability
    // on the follower's disk between the truncate and append operations (which
    // are not done atomically) when the follower processes the later request.
    uint64_t entryId = request.prev_log_index();
    for (auto it = request.entries().begin();
         it != request.entries().end();
         ++it) {
        ++entryId;
        const Protocol::Raft::Entry& entry = *it;
        if (log->getTerm(entryId) == entry.term())
            continue;
        if (log->getLastLogIndex() >= entryId) {
            // should never truncate committed entries:
            assert(commitIndex < entryId);
            // TODO(ongaro): assertion: what I'm truncating better belong to
            // only 1 term
            NOTICE("Truncating %lu entries after %lu from the log",
                   log->getLastLogIndex() - entryId + 1, entryId);
            log->truncateSuffix(entryId - 1);
            configurationDescriptions.erase(
                    configurationDescriptions.lower_bound(entryId),
                    configurationDescriptions.end());
            if (configuration->id >= entryId) {
                // Truncation removed current configuration, so fall back to
                // the prior one. We're never asked to truncate our only
                // configuration.
                auto it = configurationDescriptions.rbegin();
                assert(it != configurationDescriptions.rend());
                configuration->setConfiguration(it->first, it->second);
            }
        }
        uint64_t e = append(entry);
        assert(e == entryId);
    }

    // Set our committed ID from the request's. In rare cases, this would make
    // our committed ID decrease. For example, this could happen with a new
    // leader who has not yet replicated one of its own entries. While that'd
    // be perfectly safe, guarding against it with an if statement lets us
    // make stronger assertions.
    if (commitIndex < request.commit_index()) {
        commitIndex = request.commit_index();
        assert(commitIndex <= log->getLastLogIndex());
        stateChanged.notify_all();
        VERBOSE("New commitIndex: %lu", commitIndex);
    }
}

void
RaftConsensus::handleRequestVote(
                    const Protocol::Raft::RequestVote::Request& request,
                    Protocol::Raft::RequestVote::Response& response)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    assert(!exiting);

    if (request.term() > currentTerm) {
        VERBOSE("Caller(%lu) has newer term, updating. "
                "Ours was %lu, theirs is %lu",
                request.server_id(), currentTerm, request.term());
        stepDown(request.term());
    }

    // At this point, if leaderId != 0, we could tell the caller to step down.
    // However, this is just an optimization that does not affect correctness
    // or really even efficiency, so it's not worth the trouble.

    // If the caller has a less complete log, we can't give it our vote.
    uint64_t lastLogIndex = log->getLastLogIndex();
    uint64_t lastLogTerm = log->getTerm(lastLogIndex);
    bool logIsOk = (request.last_log_term() > lastLogTerm ||
                    (request.last_log_term() == lastLogTerm &&
                     request.last_log_index() >= lastLogIndex));

    if (request.term() == currentTerm && logIsOk && votedFor == 0) {
        // Give caller our vote
        VERBOSE("Voting for %lu in term %lu",
                request.server_id(), currentTerm);
        stepDown(currentTerm);
        setElectionTimer();
        votedFor = request.server_id();
        updateLogMetadata();
    }

    // Fill in response.
    response.set_term(currentTerm);
    // don't strictly need the first condition
    response.set_granted(request.term() == currentTerm &&
                         votedFor == request.server_id());
}

std::pair<RaftConsensus::ClientResult, uint64_t>
RaftConsensus::replicate(const std::string& operation)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    VERBOSE("replicate(%s)", operation.c_str());
    Log::Entry entry;
    entry.set_type(Protocol::Raft::EntryType::DATA);
    entry.set_data(operation);
    return replicateEntry(entry, lockGuard);
}

RaftConsensus::ClientResult
RaftConsensus::setConfiguration(
        uint64_t oldId,
        const Protocol::Raft::SimpleConfiguration& nextConfiguration)
{
    std::unique_lock<Mutex> lockGuard(mutex);

    if (state != State::LEADER)
        return ClientResult::NOT_LEADER;
    if (configuration->id != oldId ||
        configuration->state != Configuration::State::STABLE) {
        // configurations has changed in the meantime
        return ClientResult::FAIL;
    }

    uint64_t term = currentTerm;
    configuration->setStagingServers(nextConfiguration);
    stateChanged.notify_all();

    // Wait for new servers to be caught up. This will abort if not every
    // server makes progress in a ELECTION_TIMEOUT_MS period.
    ++currentEpoch;
    uint64_t epoch = currentEpoch;
    TimePoint checkProgressAt =
        Clock::now() + std::chrono::milliseconds(ELECTION_TIMEOUT_MS);
    while (true) {
        if (exiting || term != currentTerm)
            return ClientResult::NOT_LEADER;
        if (configuration->stagingAll(&Server::isCaughtUp))
            break;
        if (Clock::now() >= checkProgressAt) {
            if (configuration->stagingMin(&Server::getLastAckEpoch) < epoch) {
                configuration->resetStagingServers();
                stateChanged.notify_all();
                // TODO(ongaro): probably need to return a different type of
                // message: confuses oldId mismatch from new server down
                return ClientResult::FAIL;
            } else {
                ++currentEpoch;
                epoch = currentEpoch;
                checkProgressAt =
                    (Clock::now() +
                     std::chrono::milliseconds(ELECTION_TIMEOUT_MS));
            }
        }
        stateChanged.wait_until(lockGuard, checkProgressAt);
    }

    // Write and commit transitional configuration
    Protocol::Raft::Configuration newConfiguration;
    *newConfiguration.mutable_prev_configuration() =
        configuration->description.prev_configuration();
    *newConfiguration.mutable_next_configuration() = nextConfiguration;
    Log::Entry entry;
    entry.set_type(Protocol::Raft::EntryType::CONFIGURATION);
    *entry.mutable_configuration() = newConfiguration;
    std::pair<ClientResult, uint64_t> result =
        replicateEntry(entry, lockGuard);
    if (result.first != ClientResult::SUCCESS)
        return result.first;
    uint64_t transitionalId = result.second;

    // Wait until the configuration that removes the old servers has been
    // committed. This is the first configuration with ID greater than
    // transitionalId.
    while (true) {
        // Check this first: if the new configuration excludes us so we've
        // stepped down upon committing it, we still want to return success.
        if (configuration->id > transitionalId &&
            commitIndex >= configuration->id) {
            return ClientResult::SUCCESS;
        }
        if (exiting || term != currentTerm)
            return ClientResult::NOT_LEADER;
        stateChanged.wait(lockGuard);
    }
}

std::unique_ptr<SnapshotFile::Writer>
RaftConsensus::beginSnapshot(uint64_t lastIncludedIndex)
{
    std::unique_lock<Mutex> lockGuard(mutex);

    NOTICE("Creating new snapshot through log index %lu (inclusive)",
           lastIncludedIndex);
    std::unique_ptr<SnapshotFile::Writer> writer(
                new SnapshotFile::Writer(
                        Core::StringUtil::format("snapshot.%lu", serverId)));

    // set header fields
    SnapshotMetadata::Header header;
    header.set_last_included_index(lastIncludedIndex);
    // Find the configuration as of lastIncludedIndex. (This could potentially
    // be more efficient with more advanced use of the STL, but I'd rather do
    // the obvious backward scan through configurationDescriptions.)
    for (auto it = configurationDescriptions.rbegin();
         it != configurationDescriptions.rend();
         ++it) {
        if (it->first <= lastIncludedIndex) {
            header.set_configuration_index(it->first);
            *header.mutable_configuration() = it->second;
            break;
        }
    }

    // write header to file
    google::protobuf::io::CodedOutputStream& stream = writer->getStream();
    int size = header.ByteSize();
    stream.WriteLittleEndian32(size);
    header.SerializeWithCachedSizes(&stream);

    return writer;
}

void
RaftConsensus::snapshotDone(uint64_t lastIncludedIndex)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    lastSnapshotIndex = lastIncludedIndex;
    // TODO(ongaro): reclaim space from log here once it's safe to do so
    NOTICE("Completed snapshot through log index %lu (inclusive)",
           lastSnapshotIndex);
}

std::ostream&
operator<<(std::ostream& os, const RaftConsensus& raft)
{
    std::unique_lock<Mutex> lockGuard(raft.mutex);
    typedef RaftConsensus::State State;
    os << "server id: " << raft.serverId << std::endl;
    os << "term: " << raft.currentTerm << std::endl;
    os << "state: " << raft.state << std::endl;
    os << "leader: " << raft.leaderId << std::endl;
    os << "lastSnapshotIndex: " << raft.lastSnapshotIndex << std::endl;
    os << "commitIndex: " << raft.commitIndex << std::endl;
    switch (raft.state) {
        case State::FOLLOWER:
            os << "vote: ";
            if (raft.votedFor == 0)
                os << "available";
            else
                os << "given to " << raft.votedFor;
            os << std::endl;
            break;
        case State::CANDIDATE:
            break;
        case State::LEADER:
            break;
    }
    os << *raft.log;
    os << *raft.configuration;
    return os;
}


//// RaftConsensus private methods that MUST acquire the lock

void
RaftConsensus::timerThreadMain()
{
    std::unique_lock<Mutex> lockGuard(mutex);
    Core::ThreadId::setName("startNewElection");
    while (!exiting) {
        if (Clock::now() >= startElectionAt)
            startNewElection();
        stateChanged.wait_until(lockGuard, startElectionAt);
    }
}

void
RaftConsensus::peerThreadMain(std::shared_ptr<Peer> peer)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    Core::ThreadId::setName(
        Core::StringUtil::format("Peer(%lu)", peer->serverId));

    // Each iteration of this loop issues a new RPC or sleeps on the condition
    // variable.
    while (!peer->exiting) {
        TimePoint now = Clock::now();
        TimePoint waitUntil = TimePoint::min();

        if (peer->backoffUntil > now) {
            waitUntil = peer->backoffUntil;
        } else {
            switch (state) {
                // Followers don't issue RPCs.
                case State::FOLLOWER:
                    waitUntil = TimePoint::max();
                    break;

                // Candidates request votes.
                case State::CANDIDATE:
                    if (!peer->requestVoteDone)
                        requestVote(lockGuard, *peer);
                    else
                        waitUntil = TimePoint::max();
                    break;

                // Leaders replicate entries and periodically send heartbeats.
                case State::LEADER:
                    if (peer->getLastAgreeIndex() < log->getLastLogIndex() ||
                        peer->nextHeartbeatTime < now) {
                        appendEntries(lockGuard, *peer);
                    } else {
                        waitUntil = peer->nextHeartbeatTime;
                    }
                    break;
            }
        }

        stateChanged.wait_until(lockGuard, waitUntil);
    }

    // must return immediately after this
    --numPeerThreads;
    stateChanged.notify_all();
}

void
RaftConsensus::stepDownThreadMain()
{
    std::unique_lock<Mutex> lockGuard(mutex);
    Core::ThreadId::setName("stepDown");
    while (true) {
        // Wait until this server is the leader and is not the only server in
        // the cluster.
        while (true) {
            if (exiting)
                return;
            if (state == State::LEADER) {
                // If this local server forms a quorum (it is the only server
                // in the configuration), we need to sleep. Without this guard,
                // this method would not relinquish the CPU.
                ++currentEpoch;
                if (configuration->quorumMin(&Server::getLastAckEpoch) <
                    currentEpoch) {
                    break;
                }
            }
            stateChanged.wait(lockGuard);
        }
        // Now, if a FOLLOWER_TIMEOUT goes by without confirming leadership,
        // step down. FOLLOWER_TIMEOUT is a reasonable amount of time, since
        // it's about when other servers will start elections and bump the
        // term.
        TimePoint stepDownAt =
            Clock::now() + std::chrono::milliseconds(ELECTION_TIMEOUT_MS);
        uint64_t term = currentTerm;
        uint64_t epoch = currentEpoch; // currentEpoch was incremented above
        while (true) {
            if (exiting)
                return;
            if (currentTerm > term)
                break;
            if (configuration->quorumMin(&Server::getLastAckEpoch) >= epoch)
                break;
            if (Clock::now() >= stepDownAt) {
                NOTICE("No broadcast for a timeout, stepping down");
                stepDown(currentTerm + 1);
                break;
            }
            stateChanged.wait_until(lockGuard, stepDownAt);
        }
    }
}

//// RaftConsensus private methods that MUST NOT acquire the lock

void
RaftConsensus::advanceCommittedId()
{
    if (state != State::LEADER) {
        // getLastAgreeIndex is undefined unless we're leader
        WARNING("advanceCommittedId called as %s",
                Core::StringUtil::toString(state).c_str());
        return;
    }

    // calculate the largest entry ID stored on a quorum of servers
    uint64_t newCommittedId =
        configuration->quorumMin(&Server::getLastAgreeIndex);
    // At least one of these entries must also be from the current term to
    // guarantee that no server without them can be elected.
    if (log->getTerm(newCommittedId) != currentTerm)
        return;
    if (commitIndex >= newCommittedId)
        return;
    commitIndex = newCommittedId;
    VERBOSE("New commitIndex: %lu", commitIndex);
    assert(commitIndex <= log->getLastLogIndex());
    stateChanged.notify_all();

    if (state == State::LEADER && commitIndex >= configuration->id) {
        // Upon committing a configuration that excludes itself, the leader
        // steps down.
        if (!configuration->hasVote(configuration->localServer)) {
            stepDown(currentTerm + 1);
            return;
        }

        // Upon committing a reconfiguration (Cold,new) entry, the leader
        // creates the next configuration (Cnew) entry.
        if (configuration->state == Configuration::State::TRANSITIONAL) {
            Log::Entry entry;
            entry.set_term(currentTerm);
            entry.set_type(Protocol::Raft::EntryType::CONFIGURATION);
            *entry.mutable_configuration()->mutable_prev_configuration() =
                configuration->description.next_configuration();
            append(entry);
            advanceCommittedId();
            return;
        }
    }
}

uint64_t
RaftConsensus::append(const Log::Entry& entry)
{
    assert(entry.term() != 0);
    uint64_t entryId = log->append(entry);
    if (entry.type() == Protocol::Raft::EntryType::CONFIGURATION) {
        configurationDescriptions[entryId] = entry.configuration();
        configuration->setConfiguration(entryId, entry.configuration());
    }
    stateChanged.notify_all();
    return entryId;
}

void
RaftConsensus::appendEntries(std::unique_lock<Mutex>& lockGuard,
                           Peer& peer)
{
    // Build up request
    Protocol::Raft::AppendEntries::Request request;
    request.set_server_id(serverId);
    request.set_recipient_id(peer.serverId);
    request.set_term(currentTerm);
    uint64_t lastLogIndex = log->getLastLogIndex();
    uint64_t prevLogIndex = peer.nextIndex - 1;
    assert(prevLogIndex <= lastLogIndex);
    request.set_prev_log_term(log->getTerm(prevLogIndex));
    request.set_prev_log_index(prevLogIndex);

    // Add as many as entries as will fit comfortably in the request. It's
    // easiest to add one entry at a time until the RPC gets too big, then back
    // the last one out.
    uint64_t numEntries = 0;
    if (!peer.forceHeartbeat) {
        for (uint64_t entryId = prevLogIndex + 1;
             entryId <= lastLogIndex;
             ++entryId) {
            const Log::Entry& entry = log->getEntry(entryId);
            *request.add_entries() = entry;
            uint64_t requestSize =
                Core::Util::downCast<uint64_t>(request.ByteSize());
            if (requestSize < SOFT_RPC_SIZE_LIMIT || numEntries == 0) {
                // this entry fits, send it
                VERBOSE("sending entry <id=%lu,term=%lu>",
                        entryId, entry.term());
                ++numEntries;
            } else {
                // this entry doesn't fit, discard it
                request.mutable_entries()->RemoveLast();
            }
        }
    }
    request.set_commit_index(std::min(commitIndex, prevLogIndex + numEntries));

    // Execute RPC
    Protocol::Raft::AppendEntries::Response response;
    TimePoint start = Clock::now();
    uint64_t epoch = currentEpoch;
    bool ok = peer.callRPC(Protocol::Raft::OpCode::APPEND_ENTRIES,
                           request, response,
                           lockGuard);
    if (!ok) {
        peer.backoffUntil = start +
            std::chrono::milliseconds(RPC_FAILURE_BACKOFF_MS);
        return;
    }

    // Process response

    if (currentTerm != request.term() || peer.exiting) {
        // we don't care about result of RPC
        return;
    }
    // Since we were leader in this term before, we must still be leader in
    // this term.
    assert(state == State::LEADER);
    if (response.term() > currentTerm) {
        stepDown(response.term());
    } else {
        assert(response.term() == currentTerm);
        peer.lastAckEpoch = epoch;
        stateChanged.notify_all();
        peer.nextHeartbeatTime = start +
            std::chrono::milliseconds(HEARTBEAT_PERIOD_MS);
        if (response.success()) {
            if (peer.lastAgreeIndex > prevLogIndex + numEntries) {
                // Revisit this warning if we pipeline AppendEntries RPCs for
                // performance.
                WARNING("lastAgreeIndex should monotonically increase within a "
                        "term, since servers don't forget entries. But it "
                        "didn't.");
            } else {
                peer.lastAgreeIndex = prevLogIndex + numEntries;
                advanceCommittedId();
            }
            peer.nextIndex = peer.lastAgreeIndex + 1;
            peer.forceHeartbeat = false;

            if (!peer.isCaughtUp_ &&
                peer.thisCatchUpIterationGoalId <= peer.lastAgreeIndex) {
                Clock::duration duration =
                    Clock::now() - peer.thisCatchUpIterationStart;
                uint64_t thisCatchUpIterationMs =
                    uint64_t(std::chrono::duration_cast<
                                 std::chrono::milliseconds>(duration).count());
                if (uint64_t(labs(int64_t(peer.lastCatchUpIterationMs -
                                          thisCatchUpIterationMs))) <
                    ELECTION_TIMEOUT_MS) {
                    peer.isCaughtUp_ = true;
                    stateChanged.notify_all();
                } else {
                    peer.lastCatchUpIterationMs = thisCatchUpIterationMs;
                    peer.thisCatchUpIterationStart = Clock::now();
                    peer.thisCatchUpIterationGoalId = log->getLastLogIndex();
                }
            }
        } else {
            if (peer.nextIndex > 1)
                --peer.nextIndex;
        }
    }
}

void
RaftConsensus::becomeLeader()
{
    assert(state == State::CANDIDATE);
    NOTICE("Now leader for term %lu", currentTerm);
    state = State::LEADER;
    leaderId = serverId;
    startElectionAt = TimePoint::max();

    // Append a new entry so that commitment is not delayed indefinitely.
    // Otherwise, if the leader never gets anything to append, it will never
    // return to read-only operations (it can't prove that its committed index
    // is up-to-date).
    Log::Entry entry;
    entry.set_term(currentTerm);
    entry.set_type(Protocol::Raft::EntryType::NOOP);
    append(entry);

    // The ordering is pretty important here: First set nextIndex and
    // lastAgreeIndex for each follower, then advance the committed ID.
    // Otherwise we might advance the committed ID based on bogus values of
    // nextIndex and lastAgreeIndex.
    configuration->forEach(&Server::beginLeadership);
    advanceCommittedId(); // in case localhost forms a quorum

    // Outstanding RequestVote RPCs are no longer needed.
    interruptAll();
}

void
RaftConsensus::interruptAll()
{
    stateChanged.notify_all();
    // A configuration is sometimes missing for unit tests.
    if (configuration)
        configuration->forEach(&Server::interrupt);
}

void
RaftConsensus::readSnapshot()
{
    std::unique_ptr<SnapshotFile::Reader> reader;
    try {
        reader.reset(new SnapshotFile::Reader(
                          Core::StringUtil::format("snapshot.%lu", serverId)));
    } catch (const std::runtime_error& e) { // file not found
        NOTICE("%s", e.what());
    }
    if (reader) {
        google::protobuf::io::CodedInputStream& stream = reader->getStream();

        // read header protobuf from stream
        bool ok = true;
        uint32_t numBytes = 0;
        ok = stream.ReadLittleEndian32(&numBytes);
        if (!ok)
            PANIC("couldn't read snapshot");
        SnapshotMetadata::Header header;
        auto limit = stream.PushLimit(numBytes);
        ok = header.MergePartialFromCodedStream(&stream);
        stream.PopLimit(limit);
        if (!ok)
            PANIC("couldn't read snapshot");

        // load header contents
        if (header.last_included_index() < lastSnapshotIndex) {
            PANIC("Trying to load a snapshot that is more stale than one this "
                  "server loaded earlier. The earlier snapshot covers through "
                  "log index %lu (inclusive); this one covers through log "
                  "index %lu (inclusive)",
                  lastSnapshotIndex,
                  header.last_included_index());

        }
        lastSnapshotIndex = header.last_included_index();
        commitIndex = std::max(lastSnapshotIndex, commitIndex);
        NOTICE("Reading snapshot which covers log entries 1 through %lu "
               "(inclusive)", lastSnapshotIndex);
        if (header.has_configuration_index() && header.has_configuration()) {
            configurationDescriptions[header.configuration_index()] =
                header.configuration();
        }
    }
    if (log->getLogStartIndex() > lastSnapshotIndex + 1) {
        PANIC("The newest snapshot on this server covers up through log index "
              "%lu (inclusive), but its log starts at index %lu. This "
              "should never happen and indicates a corrupt disk state. If you "
              "want this server to participate in your cluster, you should "
              "back up all of its state, delete it, and add the server back "
              "as a new cluster member using the reconfiguration mechanism.",
              lastSnapshotIndex, log->getLogStartIndex());
    }
    snapshotReader = std::move(reader);
}

std::pair<RaftConsensus::ClientResult, uint64_t>
RaftConsensus::replicateEntry(Log::Entry& entry,
                              std::unique_lock<Mutex>& lockGuard)
{
    if (state == State::LEADER) {
        entry.set_term(currentTerm);
        uint64_t entryId = append(entry);
        advanceCommittedId();
        while (!exiting && currentTerm == entry.term()) {
            if (commitIndex >= entryId) {
                VERBOSE("replicate succeeded");
                return {ClientResult::SUCCESS, entryId};
            }
            stateChanged.wait(lockGuard);
        }
    }
    return {ClientResult::NOT_LEADER, 0};
}

void
RaftConsensus::requestVote(std::unique_lock<Mutex>& lockGuard, Peer& peer)
{
    Protocol::Raft::RequestVote::Request request;
    request.set_server_id(serverId);
    request.set_recipient_id(peer.serverId);
    request.set_term(currentTerm);
    request.set_last_log_term(log->getTerm(log->getLastLogIndex()));
    request.set_last_log_index(log->getLastLogIndex());

    Protocol::Raft::RequestVote::Response response;
    VERBOSE("requestVote start");
    TimePoint start = Clock::now();
    uint64_t epoch = currentEpoch;
    bool ok = peer.callRPC(Protocol::Raft::OpCode::REQUEST_VOTE,
                           request, response, lockGuard);
    VERBOSE("requestVote done");
    if (!ok) {
        peer.backoffUntil = start +
            std::chrono::milliseconds(RPC_FAILURE_BACKOFF_MS);
        return;
    }

    if (currentTerm != request.term() || state != State::CANDIDATE ||
        peer.exiting) {
        VERBOSE("ignore RPC result");
        // we don't care about result of RPC
        return;
    }

    if (response.term() > currentTerm) {
        stepDown(response.term());
    } else {
        peer.requestVoteDone = true;
        peer.lastAckEpoch = epoch;
        stateChanged.notify_all();

        if (response.granted()) {
            peer.haveVote_ = true;
            VERBOSE("Got vote for term %lu", currentTerm);
            if (configuration->quorumAll(&Server::haveVote))
                becomeLeader();
        } else {
            VERBOSE("vote not granted");
        }
    }
}

void
RaftConsensus::setElectionTimer()
{
    uint64_t ms = Core::Random::randomRange(ELECTION_TIMEOUT_MS,
                                            ELECTION_TIMEOUT_MS * 2);
    VERBOSE("Will become candidate in %lu ms", ms);
    startElectionAt = Clock::now() + std::chrono::milliseconds(ms);
    stateChanged.notify_all();
}

void
RaftConsensus::startNewElection()
{
    if (!configuration->hasVote(configuration->localServer)) {
        // Don't have a configuration or not part of the current configuration:
        // go back to sleep.
        return;
    }
    if (state == State::FOLLOWER) {
        // too verbose otherwise when server is partitioned
        NOTICE("Running for election in term %lu", currentTerm + 1);
    }
    ++currentTerm;
    state = State::CANDIDATE;
    leaderId = 0;
    votedFor = serverId;
    setElectionTimer();
    configuration->forEach(&Server::beginRequestVote);
    updateLogMetadata();
    interruptAll();

    // if we're the only server, this election is already done
    if (configuration->quorumAll(&Server::haveVote))
        becomeLeader();
}

void
RaftConsensus::stepDown(uint64_t newTerm)
{
    assert(currentTerm <= newTerm);
    if (currentTerm < newTerm) {
        VERBOSE("stepDown(%lu)", newTerm);
        currentTerm = newTerm;
        leaderId = 0;
        votedFor = 0;
        updateLogMetadata();
        configuration->resetStagingServers();
    }
    state = State::FOLLOWER;
    if (startElectionAt == TimePoint::max())
        setElectionTimer();
    interruptAll();
}

void
RaftConsensus::updateLogMetadata()
{
    log->metadata.set_current_term(currentTerm);
    log->metadata.set_voted_for(votedFor);
    VERBOSE("updateMetadata start");
    log->updateMetadata();
    VERBOSE("updateMetadata end");
}

bool
RaftConsensus::upToDateLeader(std::unique_lock<Mutex>& lockGuard) const
{
    ++currentEpoch;
    uint64_t epoch = currentEpoch;
    // schedule a heartbeat now so that this returns quickly
    configuration->forEach(&Server::scheduleHeartbeat);
    while (true) {
        if (exiting || state != State::LEADER)
            return false;
        // If we're the current leader and some entry from our term is
        // committed, then our commitIndex is as up-to-date as any.
        if (configuration->quorumMin(&Server::getLastAckEpoch) >= epoch &&
            log->getTerm(commitIndex) == currentTerm) {
            return true;
        }
        stateChanged.wait(lockGuard);
    }
}

std::ostream&
operator<<(std::ostream& os, RaftConsensus::ClientResult clientResult)
{
    typedef RaftConsensus::ClientResult ClientResult;
    switch (clientResult) {
        case ClientResult::SUCCESS:
            os << "ClientResult::SUCCESS";
            break;
        case ClientResult::FAIL:
            os << "ClientResult::FAIL";
            break;
        case ClientResult::RETRY:
            os << "ClientResult::RETRY";
            break;
        case ClientResult::NOT_LEADER:
            os << "ClientResult::NOT_LEADER";
            break;
    }
    return os;
}

std::ostream&
operator<<(std::ostream& os, RaftConsensus::State state)
{
    typedef RaftConsensus::State State;
    switch (state) {
        case State::FOLLOWER:
            os << "State::FOLLOWER";
            break;
        case State::CANDIDATE:
            os << "State::CANDIDATE";
            break;
        case State::LEADER:
            os << "State::LEADER";
            break;
    }
    return os;
}

} // namespace RaftConsensusInternal

} // namespace LogCabin::Server
} // namespace LogCabin
