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

#include <string.h>

#include "build/Protocol/Client.pb.h"
#include "Core/Buffer.h"
#include "Core/ProtoBuf.h"
#include "Core/Time.h"
#include "RPC/ServerRPC.h"
#include "Server/RaftConsensus.h"
#include "Server/ClientService.h"
#include "Server/Globals.h"
#include "Server/StateMachine.h"

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

    // Call the appropriate RPC handler based on the request's opCode.
    switch (rpc.getOpCode()) {
        case OpCode::GET_SERVER_INFO:
            getServerInfo(std::move(rpc));
            break;
        case OpCode::GET_SERVER_STATS:
            getServerStats(std::move(rpc));
            break;
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
            WARNING("Received RPC request with unknown opcode %u: "
                    "rejecting it as invalid request",
                    rpc.getOpCode());
            rpc.rejectInvalidRequest();
    }
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
ClientService::getServerInfo(RPC::ServerRPC rpc)
{
    PRELUDE(GetServerInfo);
    Protocol::Client::Server& info = *response.mutable_server_info();
    info.set_server_id(globals.raft->serverId);
    info.set_addresses(globals.raft->serverAddresses);
    rpc.reply(response);
}

void
ClientService::getServerStats(RPC::ServerRPC rpc)
{
    PRELUDE(GetServerStats);
    *response.mutable_server_stats() = globals.serverStats.getCurrent();
    rpc.reply(response);
}

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
    Core::Buffer cmdBuffer;
    Core::ProtoBuf::serialize(command, cmdBuffer);
    std::pair<Result, uint64_t> result = globals.raft->replicate(cmdBuffer);
    if (result.first == Result::RETRY || result.first == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        std::string leaderHint = globals.raft->getLeaderHint();
        if (!leaderHint.empty())
            error.set_leader_hint(leaderHint);
        rpc.returnError(error);
    }
    return result;
}

Result
ClientService::catchUpStateMachine(RPC::ServerRPC& rpc)
{
    std::pair<Result, uint64_t> result = globals.raft->getLastCommitIndex();
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
                           uint64_t index,
                           const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo,
                           Protocol::Client::CommandResponse& response)
{
    globals.stateMachine->wait(index);
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
        server->set_addresses(it->addresses());
    }
    rpc.reply(response);
}

void
ClientService::setConfiguration(RPC::ServerRPC rpc)
{
    PRELUDE(SetConfiguration);
    Result result = globals.raft->setConfiguration(request, response);
    if (result == Result::RETRY || result == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        std::string leaderHint = globals.raft->getLeaderHint();
        if (!leaderHint.empty())
            error.set_leader_hint(leaderHint);
        rpc.returnError(error);
        return;
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

void
ClientService::readWriteTreeRPC(RPC::ServerRPC rpc)
{
    PRELUDE(ReadWriteTree);
    Command command;
    *command.mutable_tree() = request;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    if (result.first != Result::SUCCESS)
        return;
    CommandResponse commandResponse;
    if (!getResponse(rpc, result.second, request.exactly_once(),
                     commandResponse)) {
        return;
    }
    rpc.reply(commandResponse.tree());
}



} // namespace LogCabin::Server
} // namespace LogCabin
