/* Copyright (c) 2012 Stanford University
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

#include <algorithm>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "build/Protocol/Raft.pb.h"
#include "build/Server/SnapshotMetadata.pb.h"
#include "Core/Buffer.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/Random.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"
#include "Core/Util.h"
#include "Protocol/Common.h"
#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"
#include "RPC/ServerRPC.h"
#include "Server/RaftConsensus.h"
#include "Server/Globals.h"
#include "Server/StateMachine.h"
#include "Storage/LogFactory.h"

namespace LogCabin {
namespace Server {

namespace RaftConsensusInternal {

typedef Storage::Log Log;

bool startThreads = true;

////////// Server //////////

Server::Server(uint64_t serverId)
    : serverId(serverId)
    , addresses()
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
    , lastSyncedIndex(0)
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
    lastSyncedIndex = consensus.log->getLastLogIndex();
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
    return lastSyncedIndex;
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

void
LocalServer::updatePeerStats(Protocol::ServerStats::Raft::Peer& peerStats,
                             Core::Time::SteadyTimeConverter& time) const
{
    peerStats.set_last_synced_index(lastSyncedIndex);
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
    , rpcFailuresSinceLastWarning(0)
    , lastCatchUpIterationMs(~0UL)
    , thisCatchUpIterationStart(Clock::now())
    , thisCatchUpIterationGoalId(~0UL)
    , isCaughtUp_(false)
    , snapshotFile()
    , snapshotFileOffset(0)
    , lastSnapshotIndex(0)
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
    snapshotFile.reset();
    snapshotFileOffset = 0;
    lastSnapshotIndex = 0;
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
    switch (rpc.waitForReply(&response, NULL, TimePoint::max())) {
        case RPCStatus::OK:
            if (rpcFailuresSinceLastWarning > 0) {
                WARNING("RPC to server succeeded after %lu failures",
                        rpcFailuresSinceLastWarning);
                rpcFailuresSinceLastWarning = 0;
            }
            return true;
        case RPCStatus::SERVICE_SPECIFIC_ERROR:
            PANIC("unexpected service-specific error");
        case RPCStatus::TIMEOUT:
            PANIC("unexpected RPC timeout");
        case RPCStatus::RPC_FAILED:
            ++rpcFailuresSinceLastWarning;
            if (rpcFailuresSinceLastWarning == 1) {
                WARNING("RPC to server failed: %s",
                        rpc.getErrorMessage().c_str());
            } else if (rpcFailuresSinceLastWarning % 100 == 0) {
                WARNING("Last %lu RPCs to server failed. This failure: %s",
                        rpcFailuresSinceLastWarning,
                        rpc.getErrorMessage().c_str());
            }
            return false;
        case RPCStatus::RPC_CANCELED:
            return false;
    }
    PANIC("Unexpected RPC status");
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
        RPC::Address target(addresses, Protocol::Common::DEFAULT_PORT);
        target.refresh(RPC::Address::TimePoint::max());
        session = RPC::ClientSession::makeSession(
            eventLoop,
            target,
            Protocol::Common::MAX_MESSAGE_LENGTH,
            RPC::ClientSession::TimePoint::max(),
            consensus.globals.config);
    }
    return session;
}

std::ostream&
Peer::dumpToStream(std::ostream& os) const
{
    os << "Peer " << serverId << std::endl;
    os << "addresses: " << addresses << std::endl;
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
    return os;
}

void
Peer::updatePeerStats(Protocol::ServerStats::Raft::Peer& peerStats,
                      Core::Time::SteadyTimeConverter& time) const
{
    peerStats.set_request_vote_done(requestVoteDone);
    peerStats.set_have_vote(haveVote_);
    peerStats.set_force_heartbeat(forceHeartbeat);
    peerStats.set_next_index(nextIndex);
    peerStats.set_last_agree_index(lastAgreeIndex);
    peerStats.set_is_caught_up(isCaughtUp_);

    peerStats.set_next_heartbeat_at(time.unixNanos(nextHeartbeatTime));
    peerStats.set_backoff_until(time.unixNanos(backoffUntil));
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

std::string
Configuration::lookupAddress(uint64_t serverId) const
{
    auto it = knownServers.find(serverId);
    if (it != knownServers.end())
        return it->second->addresses;
    return "";
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
Configuration::reset()
{
    state = State::BLANK;
    id = 0;
    description = {};
    oldServers.servers.clear();
    newServers.servers.clear();
    knownServers.clear();
    knownServers[localServer->serverId] = localServer;
}

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
        server->addresses = confIt->addresses();
        oldServers.servers.push_back(server);
    }

    // Build up the list of new servers
    for (auto confIt = description.next_configuration().servers().begin();
         confIt != description.next_configuration().servers().end();
         ++confIt) {
        std::shared_ptr<Server> server = getServer(confIt->server_id());
        server->addresses = confIt->addresses();
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
        server->addresses = it->addresses();
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

void
Configuration::updateServerStats(Protocol::ServerStats& serverStats,
                                 Core::Time::SteadyTimeConverter& time) const
{
    for (auto it = knownServers.begin();
         it != knownServers.end();
         ++it) {
        Protocol::ServerStats::Raft::Peer& peerStats =
            *serverStats.mutable_raft()->add_peer();
        peerStats.set_server_id(it->first);
        const ServerRef& peer = it->second;
        peerStats.set_addresses(peer->addresses);
        peerStats.set_old_member(oldServers.contains(peer));
        peerStats.set_new_member(state == State::TRANSITIONAL &&
                                 newServers.contains(peer));
        peerStats.set_staging_member(state == State::STAGING &&
                                     newServers.contains(peer));
        peer->updatePeerStats(peerStats, time);
    }
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

////////// ConfigurationManager //////////

ConfigurationManager::ConfigurationManager(Configuration& configuration)
    : configuration(configuration)
    , descriptions()
    , snapshot(0, {})
{
}

ConfigurationManager::~ConfigurationManager()
{
}

void
ConfigurationManager::add(
    uint64_t index,
    const Protocol::Raft::Configuration& description)
{
    descriptions[index] = description;
    restoreInvariants();
}

void
ConfigurationManager::truncatePrefix(uint64_t firstIndexKept)
{
    descriptions.erase(descriptions.begin(),
                       descriptions.lower_bound(firstIndexKept));
    restoreInvariants();
}

void
ConfigurationManager::truncateSuffix(uint64_t lastIndexKept)
{
    descriptions.erase(descriptions.upper_bound(lastIndexKept),
                       descriptions.end());
    restoreInvariants();
}

void
ConfigurationManager::setSnapshot(
    uint64_t index,
    const Protocol::Raft::Configuration& description)
{
    assert(index >= snapshot.first);
    snapshot = {index, description};
    restoreInvariants();
}

std::pair<uint64_t, Protocol::Raft::Configuration>
ConfigurationManager::getLatestConfigurationAsOf(
                                        uint64_t lastIncludedIndex) const
{
    if (descriptions.empty())
        return {0, {}};
    auto it = descriptions.upper_bound(lastIncludedIndex);
    // 'it' is either an element or end()
    if (it == descriptions.begin())
        return {0, {}};
    --it;
    return *it;
}

////////// ConfigurationManager private methods //////////

void
ConfigurationManager::restoreInvariants()
{
    if (snapshot.first != 0)
        descriptions.insert(snapshot);
    if (descriptions.empty()) {
        configuration.reset();
    } else {
        auto it = descriptions.rbegin();
        if (configuration.id != it->first)
            configuration.setConfiguration(it->first, it->second);
    }
}


////////// RaftConsensus::Entry //////////

RaftConsensus::Entry::Entry()
    : index(0)
    , type(SKIP)
    , command()
    , snapshotReader()
{
}

RaftConsensus::Entry::Entry(Entry&& other)
    : index(other.index)
    , type(other.type)
    , command(std::move(other.command))
    , snapshotReader(std::move(other.snapshotReader))
{
}

RaftConsensus::Entry::~Entry()
{
}


////////// RaftConsensus //////////

RaftConsensus::RaftConsensus(Globals& globals)
    : ELECTION_TIMEOUT_MS(globals.config.read<uint64_t>(
        "electionTimeoutMilliseconds", 150))
    , HEARTBEAT_PERIOD_MS(globals.config.read<uint64_t>(
        "heartbeatPeriodMilliseconds",
        ELECTION_TIMEOUT_MS / 2))
    , RPC_FAILURE_BACKOFF_MS(globals.config.read<uint64_t>(
        "rpcFailureBackoffMilliseconds",
        ELECTION_TIMEOUT_MS / 2))
    , SOFT_RPC_SIZE_LIMIT(Protocol::Common::MAX_MESSAGE_LENGTH - 1024)
    , serverId(0)
    , serverAddresses()
    , globals(globals)
    , storageLayout()
    , mutex()
    , stateChanged()
    , exiting(false)
    , numPeerThreads(0)
    , log()
    , logSyncQueued(false)
    , leaderDiskThreadWorking(false)
    , configuration()
    , configurationManager()
    , currentTerm(0)
    , state(State::FOLLOWER)
    , lastSnapshotIndex(0)
    , lastSnapshotTerm(0)
    , lastSnapshotBytes(0)
    , snapshotReader()
    , snapshotWriter()
    , commitIndex(0)
    , leaderId(0)
    , votedFor(0)
    , currentEpoch(0)
    , startElectionAt(TimePoint::max())
    , withholdVotesUntil(TimePoint::min())
    , leaderDiskThread()
    , timerThread()
    , stepDownThread()
    , invariants(*this)
{
}

RaftConsensus::~RaftConsensus()
{
    exit();
    if (leaderDiskThread.joinable())
        leaderDiskThread.join();
    if (timerThread.joinable())
        timerThread.join();
    if (stepDownThread.joinable())
        stepDownThread.join();
    std::unique_lock<Mutex> lockGuard(mutex);
    while (numPeerThreads > 0)
        stateChanged.wait(lockGuard);
    // issue any outstanding disk flushes
    if (logSyncQueued) {
        std::unique_ptr<Log::Sync> sync = log->takeSync();
        sync->wait();
        log->syncComplete(std::move(sync));
    }
}

void
RaftConsensus::init()
{
    std::unique_lock<Mutex> lockGuard(mutex);
#if DEBUG
    if (globals.config.read<bool>("raftDebug", false)) {
        mutex.callback = std::bind(&Invariants::checkAll, &invariants);
    }
#endif

    NOTICE("My server ID is %lu", serverId);

    if (storageLayout.topDir.fd == -1) {
        if (globals.config.read("use-temporary-storage", false))
            storageLayout.initTemporary(serverId); // unit tests
        else
            storageLayout.init(globals.config, serverId);
    }

    configuration.reset(new Configuration(serverId, *this));
    configurationManager.reset(new ConfigurationManager(*configuration));

    if (!log) { // some unit tests pre-set the log; don't overwrite it
        log = Storage::LogFactory::makeLog(globals.config, storageLayout);
    }
    for (uint64_t index = log->getLogStartIndex();
         index <= log->getLastLogIndex();
         ++index) {
        const Log::Entry& entry = log->getEntry(index);
        if (entry.type() == Protocol::Raft::EntryType::CONFIGURATION) {
            configurationManager->add(index, entry.configuration());
        }
    }

    NOTICE("The log contains indexes %lu through %lu (inclusive)",
           log->getLogStartIndex(), log->getLastLogIndex());

    if (log->metadata.has_current_term())
        currentTerm = log->metadata.current_term();
    if (log->metadata.has_voted_for())
        votedFor = log->metadata.voted_for();
    updateLogMetadata();

    // Read snapshot after reading log, since readSnapshot() will get rid of
    // conflicting log entries
    readSnapshot();

    if (configuration->id == 0)
        NOTICE("No configuration, waiting to receive one.");

    stepDown(currentTerm);
    if (startThreads) {
        leaderDiskThread = std::thread(&RaftConsensus::leaderDiskThreadMain,
                                       this);
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

void
RaftConsensus::bootstrapConfiguration()
{
    std::unique_lock<Mutex> lockGuard(mutex);

    if (currentTerm != 0 ||
        log->getLogStartIndex() != 1 ||
        log->getLastLogIndex() != 0 ||
        lastSnapshotIndex != 0) {
        PANIC("Refusing to bootstrap configuration: it looks like a log or "
              "snapshot already exists.");
    }
    stepDown(1); // satisfies invariants assertions

    // Append the configuration entry to the log
    Log::Entry entry;
    entry.set_term(1);
    entry.set_type(Protocol::Raft::EntryType::CONFIGURATION);
    Protocol::Raft::Configuration& configuration =
        *entry.mutable_configuration();
    Protocol::Raft::Server& server =
        *configuration.mutable_prev_configuration()->add_servers();
    server.set_server_id(serverId);
    server.set_addresses(serverAddresses);
    append({&entry});
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
RaftConsensus::getLastCommitIndex() const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    if (!upToDateLeader(lockGuard))
        return {ClientResult::NOT_LEADER, 0};
    else
        return {ClientResult::SUCCESS, commitIndex};
}

std::string
RaftConsensus::getLeaderHint() const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    return configuration->lookupAddress(leaderId);
}

RaftConsensus::Entry
RaftConsensus::getNextEntry(uint64_t lastIndex) const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    uint64_t nextIndex = lastIndex + 1;
    while (true) {
        if (exiting)
            throw Core::Util::ThreadInterruptedException();
        if (commitIndex >= nextIndex) {
            RaftConsensus::Entry entry;

            // Make the state machine load a snapshot if we don't have the next
            // entry it needs in the log.
            if (log->getLogStartIndex() > nextIndex) {
                entry.type = Entry::SNAPSHOT;
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
                entry.index = lastSnapshotIndex;
            } else {
                // not a snapshot
                const Log::Entry& logEntry = log->getEntry(nextIndex);
                entry.index = nextIndex;
                if (logEntry.type() == Protocol::Raft::EntryType::DATA) {
                    entry.type = Entry::DATA;
                    const std::string& s = logEntry.data();
                    entry.command = Core::Buffer(
                        memcpy(new char[s.length()], s.data(), s.length()),
                        s.length(),
                        Core::Buffer::deleteArrayFn<char>);
                } else {
                    entry.type = Entry::SKIP;
                }
            }
            return entry;
        }
        stateChanged.wait(lockGuard);
    }
}

SnapshotStats::SnapshotStats
RaftConsensus::getSnapshotStats() const
{
    std::unique_lock<Mutex> lockGuard(mutex);

    SnapshotStats::SnapshotStats s;
    s.set_last_snapshot_index(lastSnapshotIndex);
    s.set_last_snapshot_bytes(lastSnapshotBytes);
    s.set_log_start_index(log->getLogStartIndex());
    s.set_last_log_index(log->getLastLogIndex());
    s.set_log_bytes(log->getSizeBytes());
    s.set_is_leader(state == State::LEADER);
    return s;
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
        NOTICE("Received AppendEntries request from server %lu in term %lu "
               "(this server's term was %lu)",
                request.server_id(), request.term(), currentTerm);
        // We're about to bump our term in the stepDown below: update
        // 'response' accordingly.
        response.set_term(request.term());
    }
    // This request is a sign of life from the current leader. Update our term
    // and convert to follower if necessary; reset the election timer.
    stepDown(request.term());
    setElectionTimer();
    withholdVotesUntil = Clock::now() +
                         std::chrono::milliseconds(ELECTION_TIMEOUT_MS);

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
    // all prior entries).
    // Always match on index 0, and always match on any discarded indexes:
    // since we know those were committed, the leader must agree with them.
    // We could truncate the log here, but there's no real advantage to doing
    // that.
    if (request.prev_log_index() >= log->getLogStartIndex() &&
        log->getEntry(request.prev_log_index()).term() !=
            request.prev_log_term()) {
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
    uint64_t index = request.prev_log_index();
    for (auto it = request.entries().begin();
         it != request.entries().end();
         ++it) {
        ++index;
        const Protocol::Raft::Entry& entry = *it;
        if (index < log->getLogStartIndex()) {
            // We already snapshotted and discarded this index, so presumably
            // we've received a committed entry we once already had.
            continue;
        }
        if (log->getLastLogIndex() >= index) {
            if (log->getEntry(index).term() == entry.term())
                continue;
            // should never truncate committed entries:
            assert(commitIndex < index);
            NOTICE("Truncating %lu entries after %lu from the log",
                   log->getLastLogIndex() - index + 1,
                   index - 1);
            log->truncateSuffix(index - 1);
            configurationManager->truncateSuffix(index - 1);
        }

        // Append this and all following entries.
        std::vector<const Protocol::Raft::Entry*> entries;
        while (it != request.entries().end()) {
            entries.push_back(&*it);
            ++it;
        }
        append(entries);
        break;
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
RaftConsensus::handleInstallSnapshot(
        const Protocol::Raft::InstallSnapshot::Request& request,
        Protocol::Raft::InstallSnapshot::Response& response)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    assert(!exiting);

    response.set_term(currentTerm);

    // If the caller's term is stale, just return our term to it.
    if (request.term() < currentTerm) {
        VERBOSE("Caller(%lu) is stale. Our term is %lu, theirs is %lu",
                 request.server_id(), currentTerm, request.term());
        return;
    }
    if (request.term() > currentTerm) {
        NOTICE("Received InstallSnapshot request from server %lu in "
               "term %lu (this server's term was %lu)",
                request.server_id(), request.term(), currentTerm);
        // We're about to bump our term in the stepDown below: update
        // 'response' accordingly.
        response.set_term(request.term());
    }
    // This request is a sign of life from the current leader. Update our term
    // and convert to follower if necessary; reset the election timer.
    stepDown(request.term());
    setElectionTimer();
    withholdVotesUntil = Clock::now() +
                         std::chrono::milliseconds(ELECTION_TIMEOUT_MS);

    // Record the leader ID as a hint for clients.
    if (leaderId == 0) {
        leaderId = request.server_id();
        NOTICE("All hail leader %lu for term %lu", leaderId, currentTerm);
    } else {
        assert(leaderId == request.server_id());
    }

    if (!snapshotWriter) {
        snapshotWriter.reset(
            new Storage::SnapshotFile::Writer(storageLayout));
    }
    if (request.byte_offset() <
        uint64_t(snapshotWriter->getStream().ByteCount())) {
        WARNING("Ignoring stale snapshot chunk for byte offset %lu when the "
                "next byte needed is %lu",
                request.byte_offset(),
                uint64_t(snapshotWriter->getStream().ByteCount()));
        return;
    }
    if (request.byte_offset() >
        uint64_t(snapshotWriter->getStream().ByteCount())) {
        PANIC("Leader tried to send snapshot chunk at byte offset %lu but the "
              "next byte needed is %lu. It's supposed to send these in order.",
              request.byte_offset(),
              uint64_t(snapshotWriter->getStream().ByteCount()));
    }
    snapshotWriter->getStream().WriteString(request.data());

    if (request.done()) {
        if (request.last_snapshot_index() < lastSnapshotIndex) {
            WARNING("The leader sent us a snapshot, but it's stale: it only "
                    "covers up through index %lu and we already have one "
                    "through %lu. A well-behaved leader shouldn't do that. "
                    "Discarding the snapshot.",
                    request.last_snapshot_index(),
                    lastSnapshotIndex);
            snapshotWriter->discard();
            snapshotWriter.reset();
            return;
        }
        NOTICE("Loading in new snapshot from leader");
        snapshotWriter->save();
        snapshotWriter.reset();
        readSnapshot();
        stateChanged.notify_all();
    }
}

void
RaftConsensus::handleRequestVote(
                    const Protocol::Raft::RequestVote::Request& request,
                    Protocol::Raft::RequestVote::Response& response)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    assert(!exiting);

    if (withholdVotesUntil > Clock::now()) {
        NOTICE("Rejecting RequestVote for term %lu from server %lu, since "
               "this server (which is in term %lu) recently heard from a "
               "leader (%lu). Should server %lu be shut down?",
               request.term(), request.server_id(), currentTerm,
               leaderId, request.server_id());
        response.set_term(currentTerm);
        response.set_granted(false);
        return;
    }

    if (request.term() > currentTerm) {
        NOTICE("Received RequestVote request from server %lu in term %lu "
               "(this server's term was %lu)",
                request.server_id(), request.term(), currentTerm);
        stepDown(request.term());
    }

    // At this point, if leaderId != 0, we could tell the caller to step down.
    // However, this is just an optimization that does not affect correctness
    // or really even efficiency, so it's not worth the trouble.

    // If the caller has a less complete log, we can't give it our vote.
    uint64_t lastLogIndex = log->getLastLogIndex();
    uint64_t lastLogTerm = getLastLogTerm();
    bool logIsOk = (request.last_log_term() > lastLogTerm ||
                    (request.last_log_term() == lastLogTerm &&
                     request.last_log_index() >= lastLogIndex));

    if (request.term() == currentTerm) {
        if (logIsOk && votedFor == 0) {
            // Give caller our vote
            NOTICE("Voting for %lu in term %lu",
                   request.server_id(), currentTerm);
            stepDown(currentTerm);
            setElectionTimer();
            votedFor = request.server_id();
            updateLogMetadata();
        }
    }

    // Fill in response.
    response.set_term(currentTerm);
    // don't strictly need the first condition
    response.set_granted(request.term() == currentTerm &&
                         votedFor == request.server_id());
}

std::pair<RaftConsensus::ClientResult, uint64_t>
RaftConsensus::replicate(const Core::Buffer& operation)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    Log::Entry entry;
    entry.set_type(Protocol::Raft::EntryType::DATA);
    entry.set_data(operation.getData(), operation.getLength());
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

std::unique_ptr<Storage::SnapshotFile::Writer>
RaftConsensus::beginSnapshot(uint64_t lastIncludedIndex)
{
    std::unique_lock<Mutex> lockGuard(mutex);

    NOTICE("Creating new snapshot through log index %lu (inclusive)",
           lastIncludedIndex);
    std::unique_ptr<Storage::SnapshotFile::Writer> writer(
                new Storage::SnapshotFile::Writer(storageLayout));

    // Only committed entries may be snapshotted.
    // (This check relies on commitIndex monotonically increasing.)
    if (lastIncludedIndex > commitIndex) {
        PANIC("Attempted to snapshot uncommitted entries (%lu requested but "
              "%lu is last committed entry)", lastIncludedIndex, commitIndex);
    }

    // set header fields
    SnapshotMetadata::Header header;
    header.set_last_included_index(lastIncludedIndex);
    // Set last_included_term:
    if (lastIncludedIndex >= log->getLogStartIndex() &&
        lastIncludedIndex <= log->getLastLogIndex()) {
        header.set_last_included_term(log->getEntry(lastIncludedIndex).term());
    } else if (lastIncludedIndex == 0) {
        WARNING("Taking a snapshot covering no log entries");
        header.set_last_included_term(0);
    } else if (lastIncludedIndex == lastSnapshotIndex) {
        WARNING("Taking a snapshot where we already have one, covering "
                "entries 1 through %lu (inclusive)", lastIncludedIndex);
        header.set_last_included_term(lastSnapshotTerm);
    } else {
        WARNING("We've already discarded the entries that the state machine "
                "wants to snapshot. This can happen in rare cases if the "
                "leader already sent us a newer snapshot. We'll go ahead and "
                "compute the snapshot, but it'll be discarded later in "
                "snapshotDone(). Setting the last included term in the "
                "snapshot header to 0 (a bogus value).");
        // If this turns out to be common, we should return NULL instead and
        // change the state machines to deal with that.
        header.set_last_included_term(0);
    }
    // Copy the configuration as of lastIncludedIndex to the header.
    std::pair<uint64_t, Protocol::Raft::Configuration> c =
        configurationManager->getLatestConfigurationAsOf(lastIncludedIndex);
    if (c.first == 0) {
        WARNING("Taking snapshot with no configuration. "
                "This should have been the first thing in the log.");
    } else {
        header.set_configuration_index(c.first);
        *header.mutable_configuration() = c.second;
    }

    // write header to file
    google::protobuf::io::CodedOutputStream& stream = writer->getStream();
    int size = header.ByteSize();
    stream.WriteLittleEndian32(size);
    header.SerializeWithCachedSizes(&stream);

    return writer;
}

void
RaftConsensus::snapshotDone(
        uint64_t lastIncludedIndex,
        std::unique_ptr<Storage::SnapshotFile::Writer> writer)
{
    std::unique_lock<Mutex> lockGuard(mutex);
    if (lastIncludedIndex <= lastSnapshotIndex) {
        NOTICE("Discarding snapshot through %lu since we already have one "
               "(presumably from another server) through %lu",
               lastIncludedIndex, lastSnapshotIndex);
        writer->discard();
        return;
    }

    // log->getEntry(lastIncludedIndex) is safe:
    // If the log prefix for this snapshot was truncated, that means we have a
    // newer snapshot (handled above).
    assert(lastIncludedIndex >= log->getLogStartIndex());
    // We never truncate committed entries from the end of our log, and
    // beginSnapshot() made sure that lastIncludedIndex covers only committed
    // entries.
    assert(lastIncludedIndex <= log->getLastLogIndex());

    lastSnapshotBytes = writer->save();
    lastSnapshotIndex = lastIncludedIndex;
    lastSnapshotTerm = log->getEntry(lastIncludedIndex).term();

    // It's easier to grab this configuration out of the manager again than to
    // carry it around after writing the header.
    std::pair<uint64_t, Protocol::Raft::Configuration> c =
        configurationManager->getLatestConfigurationAsOf(lastIncludedIndex);
    if (c.first == 0) {
        WARNING("Could not find the latest configuration as of index %lu "
                "(inclusive). This shouldn't happen if the snapshot was "
                "created with a configuration, as they should be.",
                lastIncludedIndex);
    } else {
        configurationManager->setSnapshot(c.first, c.second);
    }

    NOTICE("Completed snapshot through log index %lu (inclusive)",
           lastSnapshotIndex);

    // It may be beneficial to defer discarding entries if some followers are
    // a little bit slow, to avoid having to send them a snapshot when a few
    // entries would do the trick. Best to avoid premature optimization though.
    discardUnneededEntries();
}

void
RaftConsensus::updateServerStats(Protocol::ServerStats& serverStats) const
{
    std::unique_lock<Mutex> lockGuard(mutex);
    Core::Time::SteadyTimeConverter time;
    serverStats.clear_raft();
    Protocol::ServerStats::Raft& raftStats = *serverStats.mutable_raft();

    raftStats.set_current_term(currentTerm);
    switch (state) {
        case State::FOLLOWER:
            raftStats.set_state(Protocol::ServerStats::Raft::FOLLOWER);
            break;
        case State::CANDIDATE:
            raftStats.set_state(Protocol::ServerStats::Raft::CANDIDATE);
            break;
        case State::LEADER:
            raftStats.set_state(Protocol::ServerStats::Raft::LEADER);
            break;
    }
    raftStats.set_commit_index(commitIndex);
    raftStats.set_last_log_index(log->getLastLogIndex());
    raftStats.set_leader_id(leaderId);
    raftStats.set_voted_for(votedFor);
    raftStats.set_start_election_at(time.unixNanos(startElectionAt));
    raftStats.set_withhold_votes_until(time.unixNanos(withholdVotesUntil));

    raftStats.set_last_snapshot_index(lastSnapshotIndex);
    raftStats.set_last_snapshot_bytes(lastSnapshotBytes);
    raftStats.set_log_start_index(log->getLogStartIndex());
    raftStats.set_log_bytes(log->getSizeBytes());
    configuration->updateServerStats(serverStats, time);
    log->updateServerStats(serverStats);
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
    os << "lastSnapshotTerm: " << raft.lastSnapshotTerm << std::endl;
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
RaftConsensus::leaderDiskThreadMain()
{
    std::unique_lock<Mutex> lockGuard(mutex);
    Core::ThreadId::setName("LeaderDisk");
    // Each iteration of this loop syncs the log to disk once or sleeps until
    // that is necessary.
    while (!exiting) {
        if (state == State::LEADER && logSyncQueued) {
            uint64_t term = currentTerm;
            std::unique_ptr<Log::Sync> sync = log->takeSync();
            logSyncQueued = false;
            leaderDiskThreadWorking = true;
            {
                Core::MutexUnlock<Mutex> unlockGuard(lockGuard);
                sync->wait();
                // Mark this false before re-acquiring RaftConsensus lock,
                // since stepDown() polls on this to go false while holding the
                // lock.
                leaderDiskThreadWorking = false;
            }
            if (state == State::LEADER && currentTerm == term) {
                configuration->localServer->lastSyncedIndex = sync->lastIndex;
                advanceCommitIndex();
            }
            log->syncComplete(std::move(sync));
            continue;
        }
        stateChanged.wait(lockGuard);
    }
}

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
                        // appendEntries delegates to installSnapshot if we
                        // need to send a snapshot instead
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
        // Now, if an election timeout goes by without confirming leadership,
        // step down. The election timeout is a reasonable amount of time,
        // since it's about when other servers will start elections and bump
        // the term.
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
                NOTICE("No broadcast for a timeout, stepping down from leader "
                       "of term %lu (converting to follower in term %lu)",
                       currentTerm, currentTerm + 1);
                stepDown(currentTerm + 1);
                break;
            }
            stateChanged.wait_until(lockGuard, stepDownAt);
        }
    }
}

//// RaftConsensus private methods that MUST NOT acquire the lock

void
RaftConsensus::advanceCommitIndex()
{
    if (state != State::LEADER) {
        // getLastAgreeIndex is undefined unless we're leader
        WARNING("advanceCommitIndex called as %s",
                Core::StringUtil::toString(state).c_str());
        return;
    }

    // calculate the largest entry ID stored on a quorum of servers
    uint64_t newCommitIndex =
        configuration->quorumMin(&Server::getLastAgreeIndex);
    if (commitIndex >= newCommitIndex)
        return;
    // If we have discarded the entry, it's because we already knew it was
    // committed.
    assert(newCommitIndex >= log->getLogStartIndex());
    // At least one of these entries must also be from the current term to
    // guarantee that no server without them can be elected.
    if (log->getEntry(newCommitIndex).term() != currentTerm)
        return;
    commitIndex = newCommitIndex;
    VERBOSE("New commitIndex: %lu", commitIndex);
    assert(commitIndex <= log->getLastLogIndex());
    stateChanged.notify_all();

    if (state == State::LEADER && commitIndex >= configuration->id) {
        // Upon committing a configuration that excludes itself, the leader
        // steps down.
        if (!configuration->hasVote(configuration->localServer)) {
            NOTICE("Newly committed configuration does not include self. "
                   "Stepping down as leader");
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
            append({&entry});
            return;
        }
    }
}

void
RaftConsensus::append(const std::vector<const Log::Entry*>& entries)
{
    for (auto it = entries.begin(); it != entries.end(); ++it)
        assert((*it)->term() != 0);
    std::pair<uint64_t, uint64_t> range = log->append(entries);
    if (state == State::LEADER) { // defer log sync
        logSyncQueued = true;
    } else { // sync log now
        std::unique_ptr<Log::Sync> sync = log->takeSync();
        sync->wait();
        log->syncComplete(std::move(sync));
    }
    uint64_t index = range.first;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const Log::Entry& entry = **it;
        if (entry.type() == Protocol::Raft::EntryType::CONFIGURATION)
            configurationManager->add(index, entry.configuration());
        ++index;
    }
    stateChanged.notify_all();
}

void
RaftConsensus::appendEntries(std::unique_lock<Mutex>& lockGuard,
                             Peer& peer)
{
    uint64_t lastLogIndex = log->getLastLogIndex();
    uint64_t prevLogIndex = peer.nextIndex - 1;
    assert(prevLogIndex <= lastLogIndex);

    // Don't have needed entry: send a snapshot instead.
    if (peer.nextIndex < log->getLogStartIndex()) {
        installSnapshot(lockGuard, peer);
        return;
    }

    // Find prevLogTerm or fall back to sending a snapshot.
    uint64_t prevLogTerm;
    if (prevLogIndex >= log->getLogStartIndex()) {
        prevLogTerm = log->getEntry(prevLogIndex).term();
    } else if (prevLogIndex == 0) {
        prevLogTerm = 0;
    } else if (prevLogIndex == lastSnapshotIndex) {
        prevLogTerm = lastSnapshotTerm;
    } else {
        // Don't have needed entry for prevLogTerm: send snapshot instead.
        installSnapshot(lockGuard, peer);
        return;
    }

    // Build up request
    Protocol::Raft::AppendEntries::Request request;
    request.set_server_id(serverId);
    request.set_recipient_id(peer.serverId);
    request.set_term(currentTerm);
    request.set_prev_log_term(prevLogTerm);
    request.set_prev_log_index(prevLogIndex);

    // Add as many as entries as will fit comfortably in the request. It's
    // easiest to add one entry at a time until the RPC gets too big, then back
    // the last one out.
    uint64_t numEntries = 0;
    if (!peer.forceHeartbeat) {
        for (uint64_t index = peer.nextIndex;
             index <= lastLogIndex;
             ++index) {
            const Log::Entry& entry = log->getEntry(index);
            *request.add_entries() = entry;
            uint64_t requestSize =
                Core::Util::downCast<uint64_t>(request.ByteSize());
            if (requestSize < SOFT_RPC_SIZE_LIMIT || numEntries == 0) {
                // this entry fits, send it
                VERBOSE("sending entry <index=%lu,term=%lu>",
                        index, entry.term());
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
        NOTICE("Received AppendEntries response from server %lu in term %lu "
               "(this server's term was %lu)",
                peer.serverId, response.term(), currentTerm);
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
                advanceCommitIndex();
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
RaftConsensus::installSnapshot(std::unique_lock<Mutex>& lockGuard,
                               Peer& peer)
{
    // Build up request
    Protocol::Raft::InstallSnapshot::Request request;
    request.set_server_id(serverId);
    request.set_recipient_id(peer.serverId);
    request.set_term(currentTerm);

    // Open the latest snapshot if we haven't already. Stash a copy of the
    // lastSnapshotIndex that goes along with the file, since it's possible
    // that this will change while we're transferring chunks).
    if (!peer.snapshotFile) {
        namespace FS = Storage::FilesystemUtil;
        peer.snapshotFile.reset(new FS::FileContents(
            FS::openFile(storageLayout.serverDir, "snapshot", O_RDONLY)));
        peer.snapshotFileOffset = 0;
        peer.lastSnapshotIndex = lastSnapshotIndex;
    }
    request.set_last_snapshot_index(peer.lastSnapshotIndex);
    request.set_byte_offset(peer.snapshotFileOffset);
    // The amount of data we can send is bounded by the remaining bytes in the
    // file and the maximum length for RPCs.
    uint64_t numDataBytes = std::min(
        peer.snapshotFile->getFileLength() - peer.snapshotFileOffset,
        SOFT_RPC_SIZE_LIMIT);
    request.set_data(peer.snapshotFile->get<char>(peer.snapshotFileOffset,
                                                  numDataBytes),
                     numDataBytes);
    request.set_done(peer.snapshotFileOffset + numDataBytes ==
                     peer.snapshotFile->getFileLength());

    // Execute RPC
    Protocol::Raft::InstallSnapshot::Response response;
    TimePoint start = Clock::now();
    uint64_t epoch = currentEpoch;
    bool ok = peer.callRPC(Protocol::Raft::OpCode::APPEND_SNAPSHOT_CHUNK,
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
        NOTICE("Received InstallSnapshot response from server %lu in "
               "term %lu (this server's term was %lu)",
                peer.serverId, response.term(), currentTerm);
        stepDown(response.term());
    } else {
        assert(response.term() == currentTerm);
        peer.lastAckEpoch = epoch;
        stateChanged.notify_all();
        peer.nextHeartbeatTime = start +
            std::chrono::milliseconds(HEARTBEAT_PERIOD_MS);
        peer.snapshotFileOffset += numDataBytes;
        if (request.done()) {
            peer.lastAgreeIndex = peer.lastSnapshotIndex;
            peer.nextIndex = peer.lastSnapshotIndex + 1;
            // These entries are already committed if they're in a snapshot, so
            // the commitIndex shouldn't advance, but let's just follow the
            // simple rule that bumping lastAgreeIndex should always be
            // followed by a call to advanceCommitIndex():
            advanceCommitIndex();
            peer.snapshotFile.reset();
            peer.snapshotFileOffset = 0;
            peer.lastSnapshotIndex = 0;
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
    withholdVotesUntil = TimePoint::max();

    // The ordering is pretty important here: First set nextIndex and
    // lastAgreeIndex for ourselves and each follower, then append the no op.
    // Otherwise we'll set our localServer's last agree index too high.
    configuration->forEach(&Server::beginLeadership);

    // Append a new entry so that commitment is not delayed indefinitely.
    // Otherwise, if the leader never gets anything to append, it will never
    // return to read-only operations (it can't prove that its committed index
    // is up-to-date).
    Log::Entry entry;
    entry.set_term(currentTerm);
    entry.set_type(Protocol::Raft::EntryType::NOOP);
    append({&entry});

    // Outstanding RequestVote RPCs are no longer needed.
    interruptAll();
}

void
RaftConsensus::discardUnneededEntries()
{
    if (log->getLogStartIndex() <= lastSnapshotIndex) {
        NOTICE("Removing log entries through %lu (inclusive) since "
               "they're no longer needed", lastSnapshotIndex);
        log->truncatePrefix(lastSnapshotIndex + 1);
        configurationManager->truncatePrefix(lastSnapshotIndex + 1);
        stateChanged.notify_all();
        if (state == State::LEADER) { // defer log sync
            logSyncQueued = true;
        } else { // sync log now
            std::unique_ptr<Log::Sync> sync = log->takeSync();
            sync->wait();
            log->syncComplete(std::move(sync));
        }
    }
}

uint64_t
RaftConsensus::getLastLogTerm() const
{
    uint64_t lastLogIndex = log->getLastLogIndex();
    if (lastLogIndex >= log->getLogStartIndex()) {
        return log->getEntry(lastLogIndex).term();
    } else {
        assert(lastLogIndex == lastSnapshotIndex); // potentially 0
        return lastSnapshotTerm;
    }
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
    std::unique_ptr<Storage::SnapshotFile::Reader> reader;
    if (storageLayout.serverDir.fd != -1) {
        try {
            reader.reset(new Storage::SnapshotFile::Reader(storageLayout));
        } catch (const std::runtime_error& e) { // file not found
            NOTICE("%s", e.what());
        }
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
        lastSnapshotTerm = header.last_included_term();
        lastSnapshotBytes = reader->getSizeBytes();
        commitIndex = std::max(lastSnapshotIndex, commitIndex);

        NOTICE("Reading snapshot which covers log entries 1 through %lu "
               "(inclusive)", lastSnapshotIndex);

        // We should keep log entries if they might be needed for a quorum. So:
        // 1. Discard log if it is shorter than the snapshot.
        // 2. Discard log if its lastSnapshotIndex entry disagrees with the
        //    lastSnapshotTerm.
        if (log->getLastLogIndex() < lastSnapshotIndex ||
            (log->getLogStartIndex() <= lastSnapshotIndex &&
             log->getEntry(lastSnapshotIndex).term() != lastSnapshotTerm)) {
            // The NOTICE message can be confusing if the log is empty, so
            // don't print it in that case. We still want to shift the log
            // start index, though.
            if (log->getLogStartIndex() <= log->getLastLogIndex()) {
                NOTICE("Discarding the entire log, since it's not known to be "
                       "consistent with the snapshot that is being read");
            }
            // Discard the entire log, setting the log start to point to the
            // right place.
            log->truncatePrefix(lastSnapshotIndex + 1);
            log->truncateSuffix(lastSnapshotIndex);
            configurationManager->truncatePrefix(lastSnapshotIndex + 1);
            configurationManager->truncateSuffix(lastSnapshotIndex);
            // Clean up resources.
            if (state == State::LEADER) { // defer log sync
                logSyncQueued = true;
            } else { // sync log now
                std::unique_ptr<Log::Sync> sync = log->takeSync();
                sync->wait();
                log->syncComplete(std::move(sync));
            }
        }

        discardUnneededEntries();

        if (header.has_configuration_index() && header.has_configuration()) {
            configurationManager->setSnapshot(header.configuration_index(),
                                              header.configuration());
        } else {
            WARNING("No configuration. This is unexpected, since any snapshot "
                    "should contain a configuration (they're the first thing "
                    "found in any log).");
        }

        stateChanged.notify_all();
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
        append({&entry});
        uint64_t index = log->getLastLogIndex();
        while (!exiting && currentTerm == entry.term()) {
            if (commitIndex >= index) {
                VERBOSE("replicate succeeded");
                return {ClientResult::SUCCESS, index};
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
    request.set_last_log_term(getLastLogTerm());
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
        NOTICE("Received RequestVote response from server %lu in "
               "term %lu (this server's term was %lu)",
                peer.serverId, response.term(), currentTerm);
        stepDown(response.term());
    } else {
        peer.requestVoteDone = true;
        peer.lastAckEpoch = epoch;
        stateChanged.notify_all();

        if (response.granted()) {
            peer.haveVote_ = true;
            NOTICE("Got vote from server %lu for term %lu",
                   peer.serverId, currentTerm);
            if (configuration->quorumAll(&Server::haveVote))
                becomeLeader();
        } else {
            NOTICE("Vote denied by server %lu for term %lu",
                   peer.serverId, currentTerm);
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
    if (configuration->id == 0) {
        // Don't have a configuration: go back to sleep.
        setElectionTimer();
        return;
    }

    NOTICE("Running for election in term %lu", currentTerm + 1);
    ++currentTerm;
    state = State::CANDIDATE;
    leaderId = 0;
    votedFor = serverId;
    setElectionTimer();
    configuration->forEach(&Server::beginRequestVote);
    if (snapshotWriter) {
        snapshotWriter->discard();
        snapshotWriter.reset();
    }
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
        if (snapshotWriter) {
            snapshotWriter->discard();
            snapshotWriter.reset();
        }
    }
    state = State::FOLLOWER;
    if (startElectionAt == TimePoint::max()) // was leader
        setElectionTimer();
    if (withholdVotesUntil == TimePoint::max()) // was leader
        withholdVotesUntil = TimePoint::min();
    interruptAll();

    // If the leader disk thread is currently writing to disk, wait for it to
    // finish. We poll here because we don't want to release the lock (this
    // server would then believe its writes have been flushed when they
    // haven't).
    while (leaderDiskThreadWorking)
        usleep(500);

    // If a recent append has been queued, empty it here. Do this after waiting
    // for leaderDiskThread to preserve FIFO ordering of Log::Sync objects.
    // Don't bother updating the localServer's lastSyncedIndex, since it
    // doesn't matter for non-leaders.
    if (logSyncQueued) {
        std::unique_ptr<Log::Sync> sync = log->takeSync();
        sync->wait();
        log->syncComplete(std::move(sync));
        logSyncQueued = false;
    }
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
    stateChanged.notify_all();
    while (true) {
        if (exiting || state != State::LEADER)
            return false;
        if (configuration->quorumMin(&Server::getLastAckEpoch) >= epoch) {
            // So we know we're the current leader, but do we have an
            // up-to-date commitIndex yet? What we'd like to check is whether
            // the entry's term at commitIndex matches our currentTerm, but
            // snapshots mean that we may not have the entry in our log. Since
            // commitIndex >= lastSnapshotIndex, we split into two cases:
            uint64_t commitTerm;
            if (commitIndex == lastSnapshotIndex) {
                commitTerm = lastSnapshotTerm;
            } else {
                assert(commitIndex > lastSnapshotIndex);
                assert(commitIndex >= log->getLogStartIndex());
                assert(commitIndex <= log->getLastLogIndex());
                commitTerm = log->getEntry(commitIndex).term();
            }
            if (commitTerm == currentTerm)
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
