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
#include <thread>

#include "build/Protocol/LogCabin.pb.h"
#include "Core/ProtoBuf.h"
#include "Protocol/Common.h"
#include "RPC/Buffer.h"
#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"
#include "Server/Globals.h"
#include "Server/LogManager.h"
#include "Storage/Log.h"
#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Server {
namespace {

using ProtoBuf::ClientRPC::OpCode;
typedef RPC::ClientRPC::Status Status;

class ServerClientServiceTest : public ::testing::Test {
    ServerClientServiceTest()
        : globals()
        , session()
        , thread()
    {
    }

    // Test setup is deferred for handleRPCBadOpcode, which needs to bind the
    // server port in a new thread.
    void init() {
        if (!globals) {
            globals.reset(new Globals());
            globals->config.set("storageModule", "memory");
            globals->config.set("uuid", "my-fake-uuid-123");
            globals->config.set("servers", "localhost");
            globals->init();
            session = RPC::ClientSession::makeSession(
                globals->eventLoop,
                RPC::Address("localhost", Protocol::Common::DEFAULT_PORT),
                1024 * 1024);
            thread = std::thread(&Globals::run, globals.get());
        }
    }

    ~ServerClientServiceTest()
    {
        if (globals) {
            globals->eventLoop.exit();
            thread.join();
        }
    }

    void
    call(OpCode opCode,
         const google::protobuf::Message& request,
         google::protobuf::Message& response)
    {
        RPC::ClientRPC rpc(session,
                           Protocol::Common::ServiceId::CLIENT_SERVICE,
                           1, opCode, request);
        EXPECT_EQ(Status::OK, rpc.waitForReply(&response, NULL));
    }

    std::unique_ptr<Globals> globals;
    std::shared_ptr<RPC::ClientSession> session;
    std::thread thread;
};

TEST_F(ServerClientServiceTest, handleRPCBadOpcode) {
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Request request;
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Response response;
    int bad = 255;
    OpCode unassigned = static_cast<OpCode>(bad);
    EXPECT_DEATH({init();
                  call(unassigned, request, response);},
                 "request.*invalid");
}

////////// Tests for individual RPCs //////////

TEST_F(ServerClientServiceTest, getSupportedRPCVersions) {
    init();
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Request request;
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Response response;
    call(OpCode::GET_SUPPORTED_RPC_VERSIONS, request, response);
    EXPECT_EQ("min_version: 1"
              "max_version: 1", response);
}

TEST_F(ServerClientServiceTest, openLog) {
    init();
    ProtoBuf::ClientRPC::OpenLog::Request request;
    request.set_log_name("foo");
    ProtoBuf::ClientRPC::OpenLog::Response response;
    call(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ("log_id: 1", response);
    call(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ("log_id: 1", response);
    request.set_log_name("bar");
    call(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ("log_id: 2", response);
}

TEST_F(ServerClientServiceTest, deleteLog) {
    init();
    ProtoBuf::ClientRPC::DeleteLog::Request request;
    globals->logManager.getExclusiveAccess()->createLog("foo");
    request.set_log_name("foo");
    ProtoBuf::ClientRPC::DeleteLog::Response response;
    call(OpCode::DELETE_LOG, request, response);
    call(OpCode::DELETE_LOG, request, response);
    EXPECT_EQ((std::vector<std::string> {}),
              globals->logManager.getExclusiveAccess()->listLogs());
}

TEST_F(ServerClientServiceTest, listLogs) {
    init();
    ProtoBuf::ClientRPC::ListLogs::Request request;
    ProtoBuf::ClientRPC::ListLogs::Response response;
    call(OpCode::LIST_LOGS, request, response);
    EXPECT_EQ("",
              response);
    globals->logManager.getExclusiveAccess()->createLog("foo");
    globals->logManager.getExclusiveAccess()->createLog("bar");
    EXPECT_EQ("log_names: foo"
              "log_names: bar",
              response);
}

TEST_F(ServerClientServiceTest, append) {
    init();
    globals->logManager.getExclusiveAccess()->createLog("foo");
    ProtoBuf::ClientRPC::Append::Request request;
    ProtoBuf::ClientRPC::Append::Response response;
    request.set_log_id(1);
    request.add_invalidates(10);
    request.add_invalidates(11);

    // without data append ok
    call(OpCode::APPEND, request, response);
    EXPECT_EQ("ok: { entry_id: 0 }",
              response);

    // with data append ok
    request.set_data("hello");
    call(OpCode::APPEND, request, response);
    EXPECT_EQ("ok: { entry_id: 1 }",
              response);

    // expected_entry_id mismatch
    request.set_expected_entry_id(1);
    call(OpCode::APPEND, request, response);
    EXPECT_EQ("ok: { entry_id: 0xFFFFFFFFFFFFFFFF }",
              response);

    // log disappeared
    request.set_log_id(2);
    call(OpCode::APPEND, request, response);
    EXPECT_EQ("log_disappeared: {}",
              response);

    Core::RWPtr<Storage::Log> log = globals->logManager.getExclusiveAccess()->
                                        getLogExclusive(1);
    std::deque<const Storage::LogEntry*> entries = log->readFrom(0);
    EXPECT_EQ(2U, entries.size());
    EXPECT_EQ("(1, 0) NODATA [inv 10, 11]",
              entries.at(0)->toString());
    EXPECT_EQ("(1, 1) BINARY [inv 10, 11]",
              entries.at(1)->toString());
}

TEST_F(ServerClientServiceTest, read) {
    init();
    ProtoBuf::ClientRPC::Read::Request request;
    ProtoBuf::ClientRPC::Read::Response response;
    request.set_log_id(1);
    request.set_from_entry_id(1);

    // log disappeared
    call(OpCode::READ, request, response);
    EXPECT_EQ("log_disappeared: {}",
              response);
    globals->logManager.getExclusiveAccess()->createLog("foo");

    // empty response
    call(OpCode::READ, request, response);
    EXPECT_EQ("ok: {}",
              response);

    // with and without data
    {
        auto mgr = globals->logManager.getExclusiveAccess();
        auto log = mgr->getLogExclusive(1);
        log->append(Storage::LogEntry(0, RPC::Buffer()));
        log->append(Storage::LogEntry(0, std::vector<Storage::EntryId>{}));
        char hello[] = "hello";
        log->append(Storage::LogEntry(0, RPC::Buffer(hello, 5, NULL)));
        log->append(Storage::LogEntry(0, { 10, 11 }));
    }
    call(OpCode::READ, request, response);
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
    init();
    ProtoBuf::ClientRPC::GetLastId::Request request;
    request.set_log_id(1);
    ProtoBuf::ClientRPC::GetLastId::Response response;
    call(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ("log_disappeared: {}",
              response);
    {
        auto mgr = globals->logManager.getExclusiveAccess();
        mgr->createLog("foo");
    }
    call(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ("ok: { head_entry_id: 0xFFFFFFFFFFFFFFFF }",
              response);
    {
        auto mgr = globals->logManager.getExclusiveAccess();
        auto log = mgr->getLogExclusive(1);
        log->append(Storage::LogEntry(0, RPC::Buffer()));
    }
    call(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ("ok: { head_entry_id: 0 }",
              response);
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
