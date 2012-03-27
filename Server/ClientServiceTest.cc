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

#include <gtest/gtest.h>

#include "build/Protocol/LogCabin.pb.h"
#include "Protocol/Client.h"
#include "Core/ProtoBuf.h"
#include "RPC/Buffer.h"
#include "RPC/OpaqueServerRPC.h"
#include "RPC/ProtoBuf.h"
#include "Server/ClientService.h"
#include "Server/Globals.h"
#include "Server/LogManager.h"
#include "Storage/Log.h"
#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Server {
namespace {

using ProtoBuf::ClientRPC::OpCode;
using Protocol::Client::Status;
using Protocol::Client::RequestHeaderPrefix;
using Protocol::Client::RequestHeaderVersion1;
using Protocol::Client::ResponseHeaderPrefix;
using Protocol::Client::ResponseHeaderVersion1;
using RPC::Buffer;

class ServerClientServiceTest : public ::testing::Test {
    ServerClientServiceTest()
        : globals()
        , reply()
    {
        globals.config.set("storageModule", "memory");
        globals.config.set("uuid", "my-fake-uuid-123");
        globals.config.set("servers", "localhost");
        globals.init();
    }

    Status rpc(RPC::Buffer request) {
        RPC::OpaqueServerRPC mockRPC;
        mockRPC.request = std::move(request);
        mockRPC.responseTarget = &reply;
        globals.clientService->handleRPC(std::move(mockRPC));
        EXPECT_GE(reply.getLength(), sizeof(ResponseHeaderPrefix));
        ResponseHeaderPrefix prefix =
            *static_cast<const ResponseHeaderPrefix*>(reply.getData());
        prefix.fromBigEndian();
        return Status(prefix.status);
    }

    /**
     * This is intended for testing individual RPC handlers. It assumes
     * the server returns status OK and a real response.
     */
    void rpc(OpCode opCode,
             const google::protobuf::Message& request,
             google::protobuf::Message& response) {
        RPC::Buffer requestBuffer;
        RPC::ProtoBuf::serialize(request, requestBuffer,
                                 sizeof(RequestHeaderVersion1));
        RequestHeaderVersion1& header = *static_cast<RequestHeaderVersion1*>(
                                            requestBuffer.getData());
        header.prefix.version = 1;
        header.prefix.toBigEndian();
        header.opCode = opCode;
        header.toBigEndian();
        EXPECT_EQ(Status::OK, rpc(std::move(requestBuffer)));
        EXPECT_TRUE(RPC::ProtoBuf::parse(reply, response,
                                         sizeof(ResponseHeaderVersion1)));
    }

    Globals globals;
    Buffer reply;
};

TEST_F(ServerClientServiceTest, handleRPCPrefixTooShort) {
    EXPECT_EQ(Status::INVALID_VERSION, rpc(Buffer()));
}

TEST_F(ServerClientServiceTest, handleRPCInvalidVersion) {
    RequestHeaderVersion1 header;
    header.prefix.version = 2;
    header.prefix.toBigEndian();
    header.toBigEndian();
    EXPECT_EQ(Status::INVALID_VERSION,
              rpc(Buffer(&header, sizeof(header), NULL)));
}

TEST_F(ServerClientServiceTest, handleRPCHeaderTooShort) {
    RequestHeaderPrefix prefix;
    prefix.version = 1;
    prefix.toBigEndian();
    EXPECT_EQ(Status::INVALID_VERSION,
              rpc(Buffer(&prefix, sizeof(prefix), NULL)));
}

TEST_F(ServerClientServiceTest, handleRPCNormal) {
    RequestHeaderVersion1 header;
    header.prefix.version = 1;
    header.prefix.toBigEndian();
    header.opCode = OpCode::GET_SUPPORTED_RPC_VERSIONS;
    header.toBigEndian();
    EXPECT_EQ(Status::OK,
              rpc(Buffer(&header, sizeof(header), NULL)));
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Response replyProto;
    EXPECT_TRUE(RPC::ProtoBuf::parse(reply, replyProto,
                                     sizeof(ResponseHeaderVersion1)));
    EXPECT_EQ(1U, replyProto.min_version());
    EXPECT_EQ(1U, replyProto.max_version());
}

TEST_F(ServerClientServiceTest, handleRPCBadOpcode) {
    RequestHeaderVersion1 header;
    header.prefix.version = 1;
    header.prefix.toBigEndian();
    header.opCode = 255; // this is supposed to be an unassigned opCode
    header.toBigEndian();
    EXPECT_EQ(Status::INVALID_REQUEST,
              rpc(Buffer(&header, sizeof(header), NULL)));
}

////////// Tests for individual RPCs //////////

TEST_F(ServerClientServiceTest, getSupportedRPCVersions) {
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Request request;
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Response response;
    rpc(OpCode::GET_SUPPORTED_RPC_VERSIONS, request, response);
    EXPECT_EQ("min_version: 1"
              "max_version: 1", response);
}

TEST_F(ServerClientServiceTest, openLog) {
    ProtoBuf::ClientRPC::OpenLog::Request request;
    request.set_log_name("foo");
    ProtoBuf::ClientRPC::OpenLog::Response response;
    rpc(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ("log_id: 1", response);
    rpc(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ("log_id: 1", response);
    request.set_log_name("bar");
    rpc(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ("log_id: 2", response);
}

TEST_F(ServerClientServiceTest, deleteLog) {
    ProtoBuf::ClientRPC::DeleteLog::Request request;
    globals.logManager.getExclusiveAccess()->createLog("foo");
    request.set_log_name("foo");
    ProtoBuf::ClientRPC::DeleteLog::Response response;
    rpc(OpCode::DELETE_LOG, request, response);
    rpc(OpCode::DELETE_LOG, request, response);
    EXPECT_EQ((std::vector<std::string> {}),
              globals.logManager.getExclusiveAccess()->listLogs());
}

TEST_F(ServerClientServiceTest, listLogs) {
    ProtoBuf::ClientRPC::ListLogs::Request request;
    ProtoBuf::ClientRPC::ListLogs::Response response;
    rpc(OpCode::LIST_LOGS, request, response);
    EXPECT_EQ("",
              response);
    globals.logManager.getExclusiveAccess()->createLog("foo");
    globals.logManager.getExclusiveAccess()->createLog("bar");
    EXPECT_EQ("log_names: foo"
              "log_names: bar",
              response);
}

TEST_F(ServerClientServiceTest, append) {
    globals.logManager.getExclusiveAccess()->createLog("foo");
    ProtoBuf::ClientRPC::Append::Request request;
    ProtoBuf::ClientRPC::Append::Response response;
    request.set_log_id(1);
    request.add_invalidates(10);
    request.add_invalidates(11);

    // without data append ok
    rpc(OpCode::APPEND, request, response);
    EXPECT_EQ("ok: { entry_id: 0 }",
              response);

    // with data append ok
    request.set_data("hello");
    rpc(OpCode::APPEND, request, response);
    EXPECT_EQ("ok: { entry_id: 1 }",
              response);

    // expected_entry_id mismatch
    request.set_expected_entry_id(1);
    rpc(OpCode::APPEND, request, response);
    EXPECT_EQ("ok: { entry_id: 0xFFFFFFFFFFFFFFFF }",
              response);

    // log disappeared
    request.set_log_id(2);
    rpc(OpCode::APPEND, request, response);
    EXPECT_EQ("log_disappeared: {}",
              response);

    Core::RWPtr<Storage::Log> log = globals.logManager.getExclusiveAccess()->
                                        getLogExclusive(1);
    std::deque<const Storage::LogEntry*> entries = log->readFrom(0);
    EXPECT_EQ(2U, entries.size());
    EXPECT_EQ("(1, 0) NODATA [inv 10, 11]",
              entries.at(0)->toString());
    EXPECT_EQ("(1, 1) BINARY [inv 10, 11]",
              entries.at(1)->toString());
}

TEST_F(ServerClientServiceTest, read) {
    ProtoBuf::ClientRPC::Read::Request request;
    ProtoBuf::ClientRPC::Read::Response response;
    request.set_log_id(1);
    request.set_from_entry_id(1);

    // log disappeared
    rpc(OpCode::READ, request, response);
    EXPECT_EQ("log_disappeared: {}",
              response);
    globals.logManager.getExclusiveAccess()->createLog("foo");

    // empty response
    rpc(OpCode::READ, request, response);
    EXPECT_EQ("ok: {}",
              response);

    // with and without data
    {
        auto mgr = globals.logManager.getExclusiveAccess();
        auto log = mgr->getLogExclusive(1);
        log->append(Storage::LogEntry(0, RPC::Buffer()));
        log->append(Storage::LogEntry(0, std::vector<Storage::EntryId>{}));
        char hello[] = "hello";
        log->append(Storage::LogEntry(0, RPC::Buffer(hello, 5, NULL)));
        log->append(Storage::LogEntry(0, { 10, 11 }));
    }
    rpc(OpCode::READ, request, response);
    EXPECT_EQ("ok {"
              "  entry {"
              "    entry_id: 1"
              "  }"
              "  entry {"
              "    entry_id: 2"
              "    data: 'hello'"
              "  }"
              "  entry {"
              "    entry_id: 3"
              "    invalidates: [10, 11]"
              "  }"
              "}",
              response);
}

TEST_F(ServerClientServiceTest, getLastId) {
    ProtoBuf::ClientRPC::GetLastId::Request request;
    request.set_log_id(1);
    ProtoBuf::ClientRPC::GetLastId::Response response;
    rpc(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ("log_disappeared: {}",
              response);
    {
        auto mgr = globals.logManager.getExclusiveAccess();
        mgr->createLog("foo");
    }
    rpc(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ("ok: { head_entry_id: 0xFFFFFFFFFFFFFFFF }",
              response);
    {
        auto mgr = globals.logManager.getExclusiveAccess();
        auto log = mgr->getLogExclusive(1);
        log->append(Storage::LogEntry(0, RPC::Buffer()));
    }
    rpc(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ("ok: { head_entry_id: 0 }",
              response);
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
