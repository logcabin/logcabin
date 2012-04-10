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
#include "Server/ClientService.h"
#include "Server/Globals.h"
#include "Server/LogManager.h"
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
        default:
            rpc.rejectInvalidRequest();
    }
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

void
ClientService::openLog(RPC::ServerRPC rpc)
{
    PRELUDE(OpenLog);
    Core::RWPtr<LogManager> logManager =
        globals.logManager.getExclusiveAccess();
    Storage::LogId logId = logManager->createLog(request.log_name());
    response.set_log_id(logId);
    rpc.reply(response);
}

void
ClientService::deleteLog(RPC::ServerRPC rpc)
{
    PRELUDE(DeleteLog);
    Core::RWPtr<LogManager> logManager =
        globals.logManager.getExclusiveAccess();
    logManager->deleteLog(request.log_name());
    rpc.reply(response);
}

void
ClientService::listLogs(RPC::ServerRPC rpc)
{
    PRELUDE(ListLogs);
    Core::RWPtr<const LogManager> logManager =
        globals.logManager.getSharedAccess();
    std::vector<std::string> logNames = logManager->listLogs();
    for (auto it = logNames.begin(); it != logNames.end(); ++it)
        response.add_log_names(*it);
    rpc.reply(response);
}

void
ClientService::append(RPC::ServerRPC rpc)
{
    PRELUDE(Append);

    // Optimistically prepare the log entry before grabbing locks
    Storage::LogEntry entry(0, RPC::Buffer()); // placeholder
    {
        std::vector<Storage::EntryId> invalidations(
                            request.invalidates().begin(),
                            request.invalidates().end());
        if (request.has_data()) {
            const std::string& requestData = request.data();
            RPC::Buffer data(new char[requestData.length()],
                             uint32_t(requestData.length()),
                             RPC::Buffer::deleteArrayFn<char>);
            memcpy(data.getData(), requestData.c_str(), requestData.length());
            entry = Storage::LogEntry(
                /* createTime = */ 0,
                /* data = */ std::move(data),
                /* invalidations = */ std::move(invalidations));
        } else {
            entry = Storage::LogEntry(
                /* createTime = */ 0,
                /* invalidations = */ std::move(invalidations));
        }
    }

    // Try to execute the append
    Core::RWPtr<const LogManager> logManager =
        globals.logManager.getSharedAccess();
    Core::RWPtr<Storage::Log> log =
        logManager->getLogExclusive(request.log_id());
    if (log.get() != NULL) {
        if (request.has_expected_entry_id() &&
            log->getLastId() + 1 != request.expected_entry_id()) {
            response.mutable_ok()->set_entry_id(Storage::NO_ENTRY_ID);
        } else {
            Storage::EntryId entryId = log->append(std::move(entry));
            response.mutable_ok()->set_entry_id(entryId);
        }
    } else {
        response.mutable_log_disappeared();
    }
    rpc.reply(response);
}

void
ClientService::read(RPC::ServerRPC rpc)
{
    PRELUDE(Read);
    Core::RWPtr<const LogManager> logManager =
        globals.logManager.getSharedAccess();
    Core::RWPtr<const Storage::Log> log =
        logManager->getLogShared(request.log_id());
    if (log.get() != NULL) {
        std::deque<const Storage::LogEntry*> entries =
            log->readFrom(request.from_entry_id());
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            const Storage::LogEntry& logEntry = *(*it);
            auto responseEntry = response.mutable_ok()->add_entry();
            responseEntry->set_entry_id(logEntry.entryId);
            for (auto invIt = logEntry.invalidations.begin();
                 invIt != logEntry.invalidations.end();
                 ++invIt) {
                responseEntry->add_invalidates(*invIt);
            }
            if (logEntry.hasData) {
                responseEntry->set_data(logEntry.data.getData(),
                                        logEntry.data.getLength());
            }
        }
        response.mutable_ok();
    } else {
        response.mutable_log_disappeared();
    }
    rpc.reply(response);
}

void
ClientService::getLastId(RPC::ServerRPC rpc)
{
    PRELUDE(GetLastId);
    Core::RWPtr<const LogManager> logManager =
        globals.logManager.getSharedAccess();
    Core::RWPtr<const Storage::Log> log =
        logManager->getLogShared(request.log_id());
    if (log.get() != NULL) {
        Storage::EntryId headEntryId = log->getLastId();
        response.mutable_ok()->set_head_entry_id(headEntryId);
    } else {
        response.mutable_log_disappeared();
    }
    rpc.reply(response);
}

} // namespace LogCabin::Server
} // namespace LogCabin
