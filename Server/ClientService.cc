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
#include "Storage/Log.h"
#include "Storage/LogEntry.h"

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

    // TODO(ongaro): If this is not the current cluster leader, need to
    // redirect the client.

    // Call the appropriate RPC handler based on the request's opCode.
    switch (rpc.getOpCode()) {
        case OpCode::GET_SUPPORTED_RPC_VERSIONS:
            getSupportedRPCVersions(std::move(rpc));
            break;
        case OpCode::OPEN_LOG:
            openLog(std::move(rpc));
            break;
        case OpCode::DELETE_LOG:
            deleteLog(std::move(rpc));
            break;
        case OpCode::LIST_LOGS:
            listLogs(std::move(rpc));
            break;
        case OpCode::APPEND:
            append(std::move(rpc));
            break;
        case OpCode::READ:
            read(std::move(rpc));
            break;
        case OpCode::GET_LAST_ID:
            getLastId(std::move(rpc));
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
    std::string cmdStr = Core::ProtoBuf::dumpString(command, false);
    std::pair<Result, uint64_t> result = globals.raft->replicate(cmdStr);
    if (result.first == Result::RETRY || result.first == Result::NOT_LEADER) {
        Protocol::Client::Error error;
        error.set_error_code(Protocol::Client::Error::NOT_LEADER);
        rpc.returnError(error);
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
    *command.mutable_open_session() = request;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    if (result.first != Result::SUCCESS)
        return;
    response.set_client_id(result.second);
    rpc.reply(response);
}

void
ClientService::openLog(RPC::ServerRPC rpc)
{
    PRELUDE(OpenLog);
    Command command;
    *command.mutable_open_log() = request;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    if (result.first != Result::SUCCESS)
        return;
    CommandResponse commandResponse;
    if (!getResponse(rpc, result.second, request.exactly_once(),
                     commandResponse)) {
        return;
    }
    rpc.reply(commandResponse.open_log());
}

void
ClientService::deleteLog(RPC::ServerRPC rpc)
{
    PRELUDE(DeleteLog);
    Command command;
    *command.mutable_delete_log() = request;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    if (result.first != Result::SUCCESS)
        return;
    rpc.reply(response);
}

void
ClientService::listLogs(RPC::ServerRPC rpc)
{
    PRELUDE(ListLogs);
    if (catchUpStateMachine(rpc) != Result::SUCCESS)
        return;
    globals.stateMachine->listLogs(request, response);
    rpc.reply(response);
}

void
ClientService::append(RPC::ServerRPC rpc)
{
    PRELUDE(Append);
    Command command;
    *command.mutable_append() = request;
    std::pair<Result, uint64_t> result = submit(rpc, command);
    if (result.first != Result::SUCCESS)
        return;
    CommandResponse commandResponse;
    if (!getResponse(rpc, result.second, request.exactly_once(),
                     commandResponse)) {
        return;
    }
    rpc.reply(commandResponse.append());
}

void
ClientService::read(RPC::ServerRPC rpc)
{
    PRELUDE(Read);
    if (catchUpStateMachine(rpc) != Result::SUCCESS)
        return;
    globals.stateMachine->read(request, response);
    rpc.reply(response);
}

void
ClientService::getLastId(RPC::ServerRPC rpc)
{
    PRELUDE(GetLastId);
    if (catchUpStateMachine(rpc) != Result::SUCCESS)
        return;
    globals.stateMachine->getLastId(request, response);
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
