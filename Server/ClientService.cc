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

#include <string.h>

#include "build/Protocol/Client.pb.h"
#include "RPC/Buffer.h"
#include "RPC/ProtoBuf.h"
#include "RPC/ServerRPC.h"
#include "Server/RaftConsensus.h"
#include "Server/ClientService.h"
#include "Server/Globals.h"
#include "Server/StateMachine.h"

namespace {

/**
 * Return the time since the Unix epoch in nanoseconds.
 */
uint64_t timeNanos()
{
    struct timespec now;
    int r = clock_gettime(CLOCK_REALTIME, &now);
    assert(r == 0);
    return uint64_t(now.tv_sec) * 1000 * 1000 * 1000 + now.tv_nsec;
}

}

namespace LogCabin {
namespace Server {

ClientService::ClientService(Globals& globals)
    : globals(globals)
{
}

ClientService::~ClientService()
{
}

void
ClientService::handleRPC(RPC::ServerRPC rpc)
{
    using Protocol::Client::OpCode;

    ++globals.raft->activeWorkers;

    // TODO(ongaro): If this is not the current cluster leader, need to
    // redirect the client.

    // Call the appropriate RPC handler based on the request's opCode.
    switch (rpc.getOpCode()) {
        case OpCode::GET_SUPPORTED_RPC_VERSIONS:
            getSupportedRPCVersions(std::move(rpc));
            break;
        case OpCode::GET_CONFIGURATION:
            getConfiguration(std::move(rpc));
            break;
        case OpCode::SET_CONFIGURATION:
            setConfiguration(std::move(rpc));
            break;
        case OpCode::OPEN_SESSION:
            openSession(std::move(rpc));
            break;
        case OpCode::READ_ONLY_TREE:
            readOnlyTreeRPC(std::move(rpc));
            break;
        case OpCode::READ_WRITE_TREE:
            readWriteTreeRPC(std::move(rpc));
            break;
        default:
            rpc.rejectInvalidRequest();
    }

    --globals.raft->activeWorkers;
}

std::string
ClientService::getName() const
{
    return "ClientService";
}


/**
 * Place this at the top of each RPC handler. Afterwards, 'request' will refer
 * to the protocol buffer for the request with all required fields set.
 * 'response' will be an empty protocol buffer for you to fill in the response.
 */
#define PRELUDE(rpcClass) \
    Protocol::Client::rpcClass::Request request; \
    Protocol::Client::rpcClass::Response response; \
    if (!rpc.getRequest(request)) \
        return;

////////// RPC handlers //////////


void
ClientService::getSupportedRPCVersions(RPC::ServerRPC rpc)
{
    PRELUDE(GetSupportedRPCVersions);
    response.set_min_version(1);
    response.set_max_version(1);
    rpc.reply(response);
}

typedef RaftConsensus::ClientResult Result;
typedef Protocol::Client::Command Command;
typedef Protocol::Client::CommandResponse CommandResponse;


std::pair<Result, uint64_t>
ClientService::submit(RPC::ServerRPC& rpc,
                      const google::protobuf::Message& command)
{
    RPC::Buffer contents;
    RPC::ProtoBuf::serialize(command, contents);
    // TODO: silly copy
    std::string cmdStr(static_cast<char*>(contents.getData()),
                       contents.getLength());
    std::pair<Result, uint64_t> result = globals.raft->replicate(cmdStr);
    if (result.first == Result::RETRY || result.first == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        std::string leaderHint = globals.raft->getLeaderHint();
        if (!leaderHint.empty())
            error.set_leader_hint(leaderHint);
        rpc.returnError(error);
    }
    if (result.first == Result::SUCCESS) {
        VERBOSE("%s committed at index %lu",
               cmdStr.c_str(), result.second);
    }

    return result;
}

Result
ClientService::catchUpStateMachine(RPC::ServerRPC& rpc)
{
    std::pair<Result, uint64_t> result = globals.raft->getLastCommittedId();
    if (result.first == Result::RETRY || result.first == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        std::string leaderHint = globals.raft->getLeaderHint();
        if (!leaderHint.empty())
            error.set_leader_hint(leaderHint);
        rpc.returnError(error);
        return result.first;
    }
    globals.stateMachine->wait(result.second);
    return result.first;
}

bool
ClientService::getResponse(RPC::ServerRPC& rpc,
                           uint64_t entryId,
                           const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo,
                           Protocol::Client::CommandResponse& response)
{
    VERBOSE("index %lu, %s",
            entryId,
            Core::ProtoBuf::dumpString(rpcInfo).c_str());
    globals.stateMachine->wait(entryId);
    bool ok = globals.stateMachine->getResponse(rpcInfo, response);
    if (!ok) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::SESSION_EXPIRED);
        rpc.returnError(error);
        return false;
    }
    return true;
}

void
ClientService::openSession(RPC::ServerRPC rpc)
{
    PRELUDE(OpenSession);
    Command command;
    command.set_nanoseconds_since_epoch(timeNanos());
    *command.mutable_open_session() = request;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    if (result.first != Result::SUCCESS)
        return;
    response.set_client_id(result.second);
    rpc.reply(response);
}

void
ClientService::getConfiguration(RPC::ServerRPC rpc)
{
    PRELUDE(GetConfiguration);
    Protocol::Raft::SimpleConfiguration configuration;
    uint64_t id;
    Result result = globals.raft->getConfiguration(configuration, id);
    if (result == Result::RETRY || result == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        std::string leaderHint = globals.raft->getLeaderHint();
        if (!leaderHint.empty())
            error.set_leader_hint(leaderHint);
        rpc.returnError(error);
        return;
    }
    response.set_id(id);
    for (auto it = configuration.servers().begin();
         it != configuration.servers().end();
         ++it) {
        Protocol::Client::Server* server = response.add_servers();
        server->set_server_id(it->server_id());
        server->set_address(it->address());
    }
    rpc.reply(response);
}

void
ClientService::setConfiguration(RPC::ServerRPC rpc)
{
    PRELUDE(SetConfiguration);
    Protocol::Raft::SimpleConfiguration newConfiguration;
    for (auto it = request.new_servers().begin();
         it != request.new_servers().end();
         ++it) {
        Protocol::Raft::Server* s = newConfiguration.add_servers();
        s->set_server_id(it->server_id());
        s->set_address(it->address());
    }
    Result result = globals.raft->setConfiguration(
                        request.old_id(),
                        newConfiguration);
    if (result == Result::SUCCESS) {
        response.mutable_ok();
    } else if (result == Result::RETRY || result == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        std::string leaderHint = globals.raft->getLeaderHint();
        if (!leaderHint.empty())
            error.set_leader_hint(leaderHint);
        rpc.returnError(error);
        return;
    } else if (result == Result::FAIL) {
        // TODO(ongaro): can't distinguish changed from bad
        response.mutable_configuration_changed();
    }
    rpc.reply(response);
}

void
ClientService::readOnlyTreeRPC(RPC::ServerRPC rpc)
{
    PRELUDE(ReadOnlyTree);
    if (catchUpStateMachine(rpc) != Result::SUCCESS)
        return;
    globals.stateMachine->readOnlyTreeRPC(request, response);
    rpc.reply(response);
}

#if 1
void
ClientService::readWriteTreeRPC(RPC::ServerRPC rpc)
{
#if 1
    PRELUDE(ReadWriteTree);
    Command command;
    command.set_nanoseconds_since_epoch(timeNanos());
    *command.mutable_tree() = request;
    tstat.ticksQueueStart = rdtsc();
    ++globals.raft->workersRaft;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    --globals.raft->workersRaft;
    if (result.first != Result::SUCCESS)
        return;
    ++globals.raft->workersSM;
#endif
    CommandResponse commandResponse;
#if 1
    if (!getResponse(rpc, result.second, request.exactly_once(),
                     commandResponse)) {
        return;
    }
#else
    commandResponse.mutable_tree()->set_status(Protocol::Client::Status::OK);
#endif
    --globals.raft->workersSM;
    tstat.ticksReply = rdtsc();
    ++globals.raft->workersReply;
    rpc.reply(commandResponse.tree());
    --globals.raft->workersReply;

    uint64_t total = tstat.ticksReply - tstat.ticksQueueStart;
    uint64_t good = tstat.ticksCommitted - tstat.ticksAppended;
    uint64_t queued = tstat.ticksReplicateStart - tstat.ticksQueueStart;
    uint64_t postCommit = tstat.ticksReply - tstat.ticksCommitted;
    VERBOSE("RPC total %lu ticks, good %lu%%, queued %lu%%, post-commit %lu%%",
          total,
          100 * good / total,
          100 * queued / total,
          100 * postCommit / total);
}
#else
void
ClientService::readWriteTreeRPC(RPC::ServerRPC rpc)
{
    PRELUDE(ReadWriteTree);
    Command command;
    *command.mutable_tree() = request;
    RPC::Buffer contents;
    RPC::ProtoBuf::serialize(command, contents);
    std::string cmdStr(static_cast<char*>(contents.getData()),
                       contents.getLength());
    globals.raft->replicate2(cmdStr,
                    ClientRequest { std::move(rpc), std::move(command) });
}
#endif



} // namespace LogCabin::Server
} // namespace LogCabin
