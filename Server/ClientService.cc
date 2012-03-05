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
#include <utility>

#include "../proto/dlog.pb.h"
#include "proto/Client.h"
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

namespace {

namespace ProtoBuf = DLog::ProtoBuf;
using ProtoBuf::ClientRPC::OpCode;
using Protocol::Client::RequestHeaderPrefix;
using Protocol::Client::RequestHeaderVersion1;
using Protocol::Client::ResponseHeaderPrefix;
using Protocol::Client::ResponseHeaderVersion1;
using Protocol::Client::Status;
using RPC::Buffer;
using RPC::ServerRPC;

/**
 * Reply to the RPC with a status of OK.
 * \param rpc
 *      RPC to reply to.
 * \param payload
 *      A protocol buffer to serialize into the response.
 */
void
reply(ServerRPC rpc, const google::protobuf::Message& payload)
{
    RPC::Buffer buffer;
    RPC::ProtoBuf::serialize(payload, buffer,
                             sizeof(ResponseHeaderVersion1));
    auto& responseHeader =
        *static_cast<ResponseHeaderVersion1*>(buffer.getData());
    responseHeader.prefix.status = Status::OK;
    responseHeader.prefix.toBigEndian();
    responseHeader.toBigEndian();
    rpc.response = std::move(buffer);
    rpc.sendReply();
}

/**
 * Reply to the RPC with a non-OK status.
 * \param rpc
 *      RPC to reply to.
 * \param status
 *      Status code to return.
 * \param extra
 *      Raw bytes to place after the status code. This is intended for
 *      use in Status::NOT_LEADER, which can return a string telling the client
 *      where the leader is.
 */
void
fail(ServerRPC rpc,
     Status status,
     const RPC::Buffer& extra = RPC::Buffer())
{
    uint32_t length = uint32_t(sizeof(ResponseHeaderVersion1) +
                               extra.getLength());
    RPC::Buffer buffer(new char[length],
                       length,
                       RPC::Buffer::deleteArrayFn<char>);
    auto& responseHeader =
        *static_cast<ResponseHeaderVersion1*>(buffer.getData());
    responseHeader.prefix.status = status;
    responseHeader.prefix.toBigEndian();
    responseHeader.toBigEndian();
    memcpy(&responseHeader + 1, extra.getData(), extra.getLength());
    rpc.response = std::move(buffer);
    rpc.sendReply();
}

} // anonymous namespace

ClientService::ClientService(Globals& globals)
    : globals(globals)
{
}

ClientService::~ClientService()
{
}

void
ClientService::handleRPC(ServerRPC rpc)
{
    // Carefully read the headers.
    if (rpc.request.getLength() < sizeof(RequestHeaderPrefix)) {
        fail(std::move(rpc), Status::INVALID_VERSION);
        return;
    }
    auto& requestHeaderPrefix = *static_cast<RequestHeaderPrefix*>(
                                        rpc.request.getData());
    requestHeaderPrefix.fromBigEndian();
    if (requestHeaderPrefix.version != 1 ||
        rpc.request.getLength() < sizeof(RequestHeaderVersion1)) {
        fail(std::move(rpc), Status::INVALID_VERSION);
        return;
    }
    auto& requestHeader = *static_cast<RequestHeaderVersion1*>(
                                          rpc.request.getData());
    requestHeader.fromBigEndian();

    // TODO(ongaro): If this is not the current cluster leader, need to
    // redirect the client.

    // Call the appropriate RPC handler based on the request's opCode.
    uint32_t skipBytes = uint32_t(sizeof(requestHeader));
    switch (requestHeader.opCode) {
        case OpCode::GET_SUPPORTED_RPC_VERSIONS:
            getSupportedRPCVersions(std::move(rpc), skipBytes);
            break;
        case OpCode::OPEN_LOG:
            openLog(std::move(rpc), skipBytes);
            break;
        case OpCode::DELETE_LOG:
            deleteLog(std::move(rpc), skipBytes);
            break;
        case OpCode::LIST_LOGS:
            listLogs(std::move(rpc), skipBytes);
            break;
        case OpCode::APPEND:
            append(std::move(rpc), skipBytes);
            break;
        case OpCode::READ:
            read(std::move(rpc), skipBytes);
            break;
        case OpCode::GET_LAST_ID:
            getLastId(std::move(rpc), skipBytes);
            break;
        default:
            fail(std::move(rpc), Status::INVALID_REQUEST);
    }
}

/**
 * Place this at the top of each RPC handler. Afterwards, 'request' will refer
 * to the protocol buffer for the request with all required fields set.
 * 'response' will be an empty protocol buffer for you to fill in the response.
 */
#define PRELUDE(rpcClass) \
    ProtoBuf::ClientRPC::rpcClass::Request request; \
    if (!RPC::ProtoBuf::parse(rpc.request, request, skipBytes)) \
        fail(std::move(rpc), Status::INVALID_REQUEST); \
    ProtoBuf::ClientRPC::rpcClass::Response response;

////////// RPC handlers //////////

void
ClientService::getSupportedRPCVersions(ServerRPC rpc, uint32_t skipBytes)
{
    PRELUDE(GetSupportedRPCVersions);
    response.set_min_version(1);
    response.set_max_version(1);
    reply(std::move(rpc), response);
}

void
ClientService::openLog(ServerRPC rpc, uint32_t skipBytes)
{
    PRELUDE(OpenLog);
    Core::RWPtr<LogManager> logManager =
        globals.logManager.getExclusiveAccess();
    Storage::LogId logId = logManager->createLog(request.log_name());
    response.set_log_id(logId);
    reply(std::move(rpc), response);
}

void
ClientService::deleteLog(ServerRPC rpc, uint32_t skipBytes)
{
    PRELUDE(DeleteLog);
    Core::RWPtr<LogManager> logManager =
        globals.logManager.getExclusiveAccess();
    logManager->deleteLog(request.log_name());
    reply(std::move(rpc), response);
}

void
ClientService::listLogs(ServerRPC rpc, uint32_t skipBytes)
{
    PRELUDE(ListLogs);
    Core::RWPtr<const LogManager> logManager =
        globals.logManager.getSharedAccess();
    std::vector<std::string> logNames = logManager->listLogs();
    for (auto it = logNames.begin(); it != logNames.end(); ++it)
        response.add_log_names(*it);
    reply(std::move(rpc), response);
}

void
ClientService::append(ServerRPC rpc, uint32_t skipBytes)
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
    reply(std::move(rpc), response);
}

void
ClientService::read(ServerRPC rpc, uint32_t skipBytes)
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
    reply(std::move(rpc), response);
}

void
ClientService::getLastId(ServerRPC rpc, uint32_t skipBytes)
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
    reply(std::move(rpc), response);
}

} // namespace LogCabin::Server
} // namespace LogCabin
