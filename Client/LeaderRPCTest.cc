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

#include "Client/LeaderRPC.h"
#include "Core/ProtoBuf.h"
#include "Protocol/Common.h"
#include "RPC/ClientSession.h"
#include "RPC/Server.h"
#include "RPC/ServiceMock.h"

namespace LogCabin {
namespace Client {
namespace {

using ProtoBuf::ClientRPC::OpCode;

class ClientLeaderRPCTest : public ::testing::Test {
  public:
    ClientLeaderRPCTest()
        : serverEventLoop()
        , service()
        , server()
        , serverThread()
        , leaderRPC()
        , request()
        , response()
        , expResponse()
    {
        service = std::make_shared<RPC::ServiceMock>();
        server.reset(new RPC::Server(serverEventLoop,
                                     Protocol::Common::MAX_MESSAGE_LENGTH));
        RPC::Address address("127.0.0.1", Protocol::Common::DEFAULT_PORT);
        EXPECT_EQ("", server->bind(address));
        serverThread = std::thread(&Event::Loop::runForever, &serverEventLoop);
        server->registerService(Protocol::Common::ServiceId::CLIENT_SERVICE,
                                service, 1);
        leaderRPC.reset(new LeaderRPC(address));

        request.set_log_name("logName");
        expResponse.set_log_id(3);
    }
    ~ClientLeaderRPCTest()
    {
        serverEventLoop.exit();
        serverThread.join();
    }

    Event::Loop serverEventLoop;
    std::shared_ptr<RPC::ServiceMock> service;
    std::unique_ptr<RPC::Server> server;
    std::thread serverThread;
    std::unique_ptr<LeaderRPC> leaderRPC;
    ProtoBuf::ClientRPC::OpenLog::Request request;
    ProtoBuf::ClientRPC::OpenLog::Response response;
    ProtoBuf::ClientRPC::OpenLog::Response expResponse;
};

// constructor and destructor tested adequately in tests for call()

TEST_F(ClientLeaderRPCTest, callOK) {
    service->reply(OpCode::OPEN_LOG, request, expResponse);
    leaderRPC->call(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ(expResponse, response);
}

// For service-specific error case,
// see handleServiceSpecificErrorNotLeader below.

TEST_F(ClientLeaderRPCTest, callRPCFailed) {
    service->closeSession(OpCode::OPEN_LOG, request);
    service->reply(OpCode::OPEN_LOG, request, expResponse);
    leaderRPC->call(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ(expResponse, response);
}

TEST_F(ClientLeaderRPCTest, handleServiceSpecificErrorNotLeader) {
    ProtoBuf::ClientRPC::Error error;
    error.set_error_code(ProtoBuf::ClientRPC::Error::NOT_LEADER);

    // no hint
    service->serviceSpecificError(OpCode::OPEN_LOG, request, error);

    // sucky hint
    error.set_leader_hint("127.0.0.1:0");
    service->serviceSpecificError(OpCode::OPEN_LOG, request, error);

    // ok, fine, let it through
    service->reply(OpCode::OPEN_LOG, request, expResponse);

    leaderRPC->call(OpCode::OPEN_LOG, request, response);
    EXPECT_EQ(expResponse, response);
}

// connect() tested adequately in tests for call()

TEST_F(ClientLeaderRPCTest, connectRandom) {
    // TODO(ongaro): This is hard to test without control of name resolution.
}

TEST_F(ClientLeaderRPCTest, connectHost) {
    leaderRPC->connectHost("127.0.0.2:0", leaderRPC->leaderSession);
    EXPECT_EQ("Closed session: Failed to connect socket to 127.0.0.2:0 "
              "(resolved to 127.0.0.2:0)",
              leaderRPC->leaderSession->toString());
}


} // namespace LogCabin::Client::<anonymous>
} // namespace LogCabin::Client
} // namespace LogCabin
