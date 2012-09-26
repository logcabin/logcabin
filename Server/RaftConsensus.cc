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
LocalServer::exit()
{
}

uint64_t
LocalServer::getLastAckEpoch() const
{
    return consensus.currentEpoch;
}

uint64_t
LocalServer::getLastAgreeId() const
{
    return consensus.log->getLastLogId();
}

bool
LocalServer::haveVote() const
{
    return (consensus.votedFor == serverId);
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

////////// Peer //////////

Peer::Peer(uint64_t serverId, RaftConsensus& consensus)
    : Server(serverId)
    , consensus(consensus)
    , eventLoop(consensus.globals.eventLoop)
    , exiting(false)
    , requestVoteDone(false)
    , haveVote_(false)
    , lastAgreeId(0)
    , lastAckEpoch(0)
    , nextHeartbeatTime(TimePoint::min())
    , backoffUntil(TimePoint::min())
    , lastCatchUpIterationMs(~0UL)
    , thisCatchUpIterationStart(Clock::now())
    , thisCatchUpIterationGoalId(~0UL)
    , isCaughtUp_(false)
    , session()
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
    lastAgreeId = 0;
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
Peer::getLastAgreeId() const
{
    return lastAgreeId;
}

bool
Peer::haveVote() const
{
    return haveVote_;
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
        google::protobuf::Message& response)
{
    typedef RPC::ClientRPC::Status RPCStatus;

    RPC::ClientRPC rpc(getSession(),
                       Protocol::Common::ServiceId::RAFT_SERVICE,
                       /* serviceSpecificErrorVersion = */ 0,
                       opCode,
                       request);
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
    thisCatchUpIterationGoalId = consensus.log->getLastLogId();
    ++consensus.numPeerThreads;
    thread = std::thread(&RaftConsensus::followerThreadMain, &consensus, self);
    thread.detach();
}

std::shared_ptr<RPC::ClientSession>
Peer::getSession()
{
    if (!session || !session->getErrorMessage().empty()) {
        session = RPC::ClientSession::makeSession(
            eventLoop,
            RPC::Address(address, Protocol::Common::DEFAULT_PORT),
            Protocol::Common::MAX_MESSAGE_LENGTH);
    }
    return session;
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
        if (!predicate(*it))
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
        sideEffect(*it);
}

uint64_t
Configuration::SimpleConfiguration::min(const GetValue& getValue) const
{
    if (servers.empty())
        return 0;
    uint64_t smallest = ~0UL;
    for (auto it = servers.begin(); it != servers.end(); ++it)
        smallest = std::min(smallest, getValue(*it));
    return smallest;
}

bool
Configuration::SimpleConfiguration::quorumAll(const Predicate& predicate) const
{
    if (servers.empty())
        return true;
    uint64_t count = 0;
    for (auto it = servers.begin(); it != servers.end(); ++it)
        if (predicate(*it))
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
        values.push_back(getValue(*it));
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
        sideEffect(it->second);
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
void setGCFlag(std::shared_ptr<Server> server)
{
    server->gcFlag = true;
}
} // anonymous namespace

void
Configuration::setConfiguration(
        uint64_t newId,
        const Protocol::Raft::Configuration& newDescription)
{
    NOTICE("Activating configuration %lu:\n%s", newId,
           Core::ProtoBuf::dumpString(newDescription, false).c_str());

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
    setGCFlag(localServer);
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
    , configuration()
    , currentTerm(0)
    , state(State::FOLLOWER)
    , committedId(0)
    , leaderId(0)
    , votedFor(0)
    , currentEpoch(0)
    , startElectionAt(TimePoint::max())
    , candidacyThread()
    , stepDownThread()
    , invariants(*this)
{
}

RaftConsensus::~RaftConsensus()
{
    exit();
    if (candidacyThread.joinable())
        candidacyThread.join();
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
        log.reset(new Log(Core::StringUtil::format("log/%lu", serverId)));
    }
    NOTICE("Last log ID: %lu", log->getLastLogId());
    if (log->metadata.has_current_term())
        currentTerm = log->metadata.current_term();
    if (log->metadata.has_voted_for())
        votedFor = log->metadata.voted_for();
    updateLogMetadata();

    configuration.reset(new Configuration(serverId, *this));
    scanForConfiguration();
    if (configuration->state == Configuration::State::BLANK)
        NOTICE("No configuration, waiting to receive one");

    stepDown(currentTerm);
    if (startThreads) {
        candidacyThread = std::thread(&RaftConsensus::candidacyThreadMain,
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
        committedId < configuration->id) {
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
        return {ClientResult::SUCCESS, committedId};
}

Consensus::Entry
RaftConsensus::getNextEntry(uint64_t lastEntryId) const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    uint64_t nextEntryId = lastEntryId + 1;
    while (true) {
        if (exiting)
            throw ThreadInterruptedException();
        if (committedId >= nextEntryId) {
            const Log::Entry& logEntry = log->getEntry(nextEntryId);
            Consensus::Entry entry;
            entry.entryId = logEntry.entryId;
            if (logEntry.type == Protocol::Raft::EntryType::DATA) {
                entry.hasData = true;
                entry.data = logEntry.data;
            }
            return entry;
        }
        stateChanged.wait(lockGuard);
    }
}

void
RaftConsensus::handleAppendEntry(
                    const Protocol::Raft::AppendEntry::Request& request,
                    Protocol::Raft::AppendEntry::Response& response)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    assert(!exiting);

    // If the caller's term is stale, just return our term to it.
    if (request.term() < currentTerm) {
        VERBOSE("Caller(%lu) is stale. Our term is %lu, theirs is %lu",
                 request.server_id(), currentTerm, request.term());
        response.set_term(currentTerm);
        return;
    }

    // A new leader should always calls requestVote on this server before
    // calling appendEntry; thus, we should not see a new term here.
    assert(request.term() == currentTerm);

    // Record the leader ID as a hint for clients.
    if (leaderId == 0) {
        leaderId = request.server_id();
        NOTICE("All hail leader %lu for term %lu", leaderId, currentTerm);
    } else {
        assert(leaderId == request.server_id());
    }

    // This request is a sign of life from the current leader.
    stepDown(currentTerm);
    setElectionTimer();

    // For an entry to fit into our log, it must not leave a gap.
    assert(request.prev_log_id() <= log->getLastLogId());
    // It must also agree with the previous entry in the log (and, inductively
    // all prior entries).
    assert(log->getTerm(request.prev_log_id()) == request.prev_log_term());

    // This needs to be able to handle duplicated RPC requests. We compare the
    // entries' terms to know if we need to do the operation; otherwise,
    // reapplying requests can result in data loss.
    //
    // The first problem this solves is that an old AppendEntry request may be
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
    uint64_t entryId = request.prev_log_id();
    for (auto it = request.entries().begin();
         it != request.entries().end();
         ++it) {
        ++entryId;
        if (log->getTerm(entryId) == it->term())
            continue;
        if (log->getLastLogId() >= entryId) {
            assert(committedId < entryId);
            // TODO(ongaro): assertion: what I'm truncating better belong to
            // only 1 term
            NOTICE("Truncating %lu entries",
                   log->getLastLogId() - entryId + 1);
            log->truncate(entryId - 1);
            if (configuration->id >= entryId) {
                // truncate can affect current configuration
                scanForConfiguration();
            }
        }
        Log::Entry entry;
        entry.term = it->term();
        entry.type = it->type();
        switch (entry.type) {
            case Protocol::Raft::EntryType::CONFIGURATION:
                entry.configuration = it->configuration();
                break;
            case Protocol::Raft::EntryType::DATA:
                entry.data = it->data();
                break;
            default:
                PANIC("bad entry type");
        }
        uint64_t e = append(entry);
        assert(e == entryId);
    }

    // The request's committed ID may be lower than ours: the protocol
    // guarantees that a server with all committed entries becomes the new
    // leader, but it does not guarantee that the new leader has the largest
    // committed ID. We want our committed ID to monotonically increase over
    // time, so we only update it if the request's is larger.
    if (committedId < request.committed_id()) {
        committedId = request.committed_id();
        assert(committedId <= log->getLastLogId());
        stateChanged.notify_all();
        VERBOSE("New committedId: %lu", committedId);
    }

    response.set_term(currentTerm);
}

void
RaftConsensus::handleRequestVote(
                    const Protocol::Raft::RequestVote::Request& request,
                    Protocol::Raft::RequestVote::Response& response)
{
    std::unique_lock<Mutex> lockGuard(mutex);

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
    uint64_t lastLogId = log->getLastLogId();
    uint64_t lastLogTerm = log->getTerm(lastLogId);
    bool logIsOk = (request.last_log_term() > lastLogTerm ||
                    (request.last_log_term() == lastLogTerm &&
                     request.last_log_id() >= lastLogId));

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
    response.set_last_log_term(lastLogTerm);
    response.set_last_log_id(lastLogId);
    response.set_begin_last_term_id(log->getBeginLastTermId());
}

std::pair<RaftConsensus::ClientResult, uint64_t>
RaftConsensus::replicate(const std::string& operation)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    VERBOSE("replicate(%s)", operation.c_str());
    Log::Entry entry;
    entry.type = Protocol::Raft::EntryType::DATA;
    entry.data = operation;
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
    entry.type = Protocol::Raft::EntryType::CONFIGURATION;
    entry.configuration = newConfiguration;
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
            committedId >= configuration->id) {
            return ClientResult::SUCCESS;
        }
        if (exiting || term != currentTerm)
            return ClientResult::NOT_LEADER;
        stateChanged.wait(lockGuard);
    }
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
        case State::LEADER: {
            break;
        }
    }
    return os;
}


//// RaftConsensus private methods that MUST acquire the lock

void
RaftConsensus::candidacyThreadMain()
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
RaftConsensus::followerThreadMain(std::shared_ptr<Peer> peer)
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

                // Leaders use requestVote to get the follower's log info,
                // then replicate data and periodically send heartbeats.
                case State::LEADER:
                    if (!peer->requestVoteDone) {
                        requestVote(lockGuard, *peer);
                    } else {
                        if (peer->lastAgreeId == log->getLastLogId() &&
                            now < peer->nextHeartbeatTime) {
                            waitUntil = peer->nextHeartbeatTime;
                        } else {
                            appendEntry(lockGuard, *peer);
                        }
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
        // getLastAgreeId is undefined unless we're leader
        WARNING("advanceCommittedId called as %s",
                Core::StringUtil::toString(state).c_str());
        return;
    }

    // calculate the largest entry ID stored on a quorum of servers
    uint64_t newCommittedId =
        configuration->quorumMin(&Server::getLastAgreeId);
    if (committedId >= newCommittedId)
        return;
    committedId = newCommittedId;
    VERBOSE("New committedId: %lu", committedId);
    assert(committedId <= log->getLastLogId());
    stateChanged.notify_all();

    if (state == State::LEADER && committedId >= configuration->id) {
        // Upon committing a configuration that excludes itself, the leader
        // steps down.
        if (!configuration->hasVote(configuration->localServer)) {
            stepDown(currentTerm + 1);
            return;
        }

        // Upon committing a reconfiguration (Cold,new) entry, the leader
        // creates the next configuration (Cnew) entry.
        if (configuration->state == Configuration::State::TRANSITIONAL &&
            isLeaderReady()) {
            Log::Entry entry;
            entry.term = currentTerm;
            entry.type = Protocol::Raft::EntryType::CONFIGURATION;
            *entry.configuration.mutable_prev_configuration() =
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
    assert(entry.term != 0);
    uint64_t entryId = log->append(entry);
    if (entry.type == Protocol::Raft::EntryType::CONFIGURATION)
        configuration->setConfiguration(entryId, entry.configuration);
    stateChanged.notify_all();
    return entryId;
}

void
RaftConsensus::appendEntry(std::unique_lock<Mutex>& lockGuard,
                           Peer& peer)
{
    // Build up request
    Protocol::Raft::AppendEntry::Request request;
    request.set_server_id(serverId);
    request.set_term(currentTerm);
    uint64_t lastLogId = log->getLastLogId();
    uint64_t prevLogId = peer.lastAgreeId;
    request.set_prev_log_term(log->getTerm(prevLogId));
    request.set_prev_log_id(prevLogId);
    // Add entries
    uint64_t numEntries = 0;
    for (uint64_t entryId = prevLogId + 1; entryId <= lastLogId; ++entryId) {
        const Log::Entry& entry = log->getEntry(entryId);
        Protocol::Raft::Entry* e = request.add_entries();
        e->set_term(entry.term);
        e->set_type(entry.type);
        switch (entry.type) {
            case Protocol::Raft::EntryType::CONFIGURATION:
                *e->mutable_configuration() = entry.configuration;
                break;
            case Protocol::Raft::EntryType::DATA:
                e->set_data(entry.data);
                break;
            default:
                PANIC("bad entry type");
        }
        uint64_t requestSize =
            Core::Util::downCast<uint64_t>(request.ByteSize());
        if (requestSize < SOFT_RPC_SIZE_LIMIT || numEntries == 0) {
            // this entry fits, send it
            VERBOSE("sending entry <id=%lu,term=%lu>", entryId, entry.term);
            ++numEntries;
        } else {
            // this entry doesn't fit, discard it
            request.mutable_entries()->RemoveLast();
        }
    }
    request.set_committed_id(std::min(committedId, prevLogId + numEntries));

    // Execute RPC
    Protocol::Raft::AppendEntry::Response response;
    TimePoint start = Clock::now();
    uint64_t epoch = currentEpoch;
    lockGuard.unlock();
    bool ok = peer.callRPC(Protocol::Raft::OpCode::APPEND_ENTRY,
                           request, response);
    lockGuard.lock();
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
        peer.lastAgreeId += numEntries;
        advanceCommittedId();

        if (!peer.isCaughtUp_ &&
            peer.thisCatchUpIterationGoalId <= peer.lastAgreeId) {
            Clock::duration duration =
                Clock::now() - peer.thisCatchUpIterationStart;
            uint64_t thisCatchUpIterationMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    duration).count();
            if (uint64_t(labs(peer.lastCatchUpIterationMs -
                              thisCatchUpIterationMs)) < ELECTION_TIMEOUT_MS) {
                peer.isCaughtUp_ = true;
                stateChanged.notify_all();
            } else {
                peer.lastCatchUpIterationMs = thisCatchUpIterationMs;
                peer.thisCatchUpIterationStart = Clock::now();
                peer.thisCatchUpIterationGoalId = log->getLastLogId();
            }
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
    advanceCommittedId();
    stateChanged.notify_all();
}

void
RaftConsensus::interruptAll()
{
    stateChanged.notify_all();
    // TODO(ongaro): ideally, this would go abort any current RPC, but RPC
    // objects aren't presently thread-safe
}

bool
RaftConsensus::isLeaderReady() const
{
    assert(state == State::LEADER);
    uint64_t firstUncommittedTerm = log->getTerm(committedId + 1);
    return (firstUncommittedTerm == 0 || firstUncommittedTerm == currentTerm);
}

std::pair<RaftConsensus::ClientResult, uint64_t>
RaftConsensus::replicateEntry(Log::Entry& entry,
                              std::unique_lock<Mutex>& lockGuard)
{
    if (state == State::LEADER) {
        if (!isLeaderReady())
            return {ClientResult::RETRY, 0};
        entry.term = currentTerm;
        uint64_t entryId = append(entry);
        advanceCommittedId();
        while (!exiting && currentTerm == entry.term) {
            if (committedId >= entryId) {
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
    request.set_term(currentTerm);
    request.set_last_log_term(log->getTerm(log->getLastLogId()));
    request.set_last_log_id(log->getLastLogId());

    Protocol::Raft::RequestVote::Response response;
    lockGuard.unlock();
    VERBOSE("requestVote start");
    TimePoint start = Clock::now();
    uint64_t epoch = currentEpoch;
    bool ok = peer.callRPC(Protocol::Raft::OpCode::REQUEST_VOTE,
                                    request, response);
    VERBOSE("requestVote done");
    lockGuard.lock();
    if (!ok) {
        peer.backoffUntil = start +
            std::chrono::milliseconds(RPC_FAILURE_BACKOFF_MS);
        return;
    }

    if (currentTerm != request.term() || state == State::FOLLOWER ||
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

        // The peer's response.begin_last_term_id() - 1 is committed, since
        // only entries in the peer's last term might be uncommitted. If we
        // successfully become leader, we also have it. If not, lastAgreeId is
        // undefined anyway.
        peer.lastAgreeId = response.begin_last_term_id() - 1;
        // Beyond that, we agree with the peer on any entries that match terms
        // with ours.
        for (uint64_t entryId = response.begin_last_term_id();
             entryId <= response.last_log_id();
             ++entryId) {
            if (log->getTerm(entryId) != response.last_log_term())
                break;
            peer.lastAgreeId = entryId;
        }

        if (state == State::LEADER) {
            advanceCommittedId();
        } else {
            assert(state == State::CANDIDATE);
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
}

void
RaftConsensus::scanForConfiguration()
{
    for (uint64_t entryId = log->getLastLogId(); entryId > 0; --entryId) {
        const Log::Entry& entry = log->getEntry(entryId);
        if (entry.type == Protocol::Raft::EntryType::CONFIGURATION) {
            configuration->setConfiguration(entryId, entry.configuration);
            return;
        }
    }
    // the configuration is never cleared out
    assert(configuration->state == Configuration::State::BLANK);
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
        if (configuration->quorumMin(&Server::getLastAckEpoch) >=
            epoch) {
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
