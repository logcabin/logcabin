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
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Protocol/Common.h"
#include "RPC/ClientSession.h"
#include "RPC/Server.h"
#include "RPC/ServiceMock.h"

namespace LogCabin {
namespace Client {
namespace {

using Protocol::Client::OpCode;

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
        server->registerService(Protocol::Common::ServiceId::CLIENT_SERVICE,
                                service, 1);
        leaderRPC.reset(new LeaderRPC(address));

        request.set_log_id(3);
        expResponse.mutable_ok()->set_head_entry_id(4);
    }
    ~ClientLeaderRPCTest()
    {
        serverEventLoop.exit();
        if (serverThread.joinable())
            serverThread.join();
    }

    void init() {
        serverThread = std::thread(&Event::Loop::runForever, &serverEventLoop);
    }

    Event::Loop serverEventLoop;
    std::shared_ptr<RPC::ServiceMock> service;
    std::unique_ptr<RPC::Server> server;
    std::thread serverThread;
    std::unique_ptr<LeaderRPC> leaderRPC;
    Protocol::Client::GetLastId::Request request;
    Protocol::Client::GetLastId::Response response;
    Protocol::Client::GetLastId::Response expResponse;
};

// constructor and destructor tested adequately in tests for call()

TEST_F(ClientLeaderRPCTest, callOK) {
    init();
    service->reply(OpCode::GET_LAST_ID, request, expResponse);
    leaderRPC->call(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ(expResponse, response);
}

// For service-specific error case,
// see handleServiceSpecificErrorNotLeader below.

TEST_F(ClientLeaderRPCTest, callRPCFailed) {
    init();
    service->closeSession(OpCode::GET_LAST_ID, request);
    service->reply(OpCode::GET_LAST_ID, request, expResponse);
    leaderRPC->call(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ(expResponse, response);
}

TEST_F(ClientLeaderRPCTest, handleServiceSpecificErrorNotLeader) {
    init();
    Protocol::Client::Error error;
    error.set_error_code(Protocol::Client::Error::NOT_LEADER);

    // no hint
    service->serviceSpecificError(OpCode::GET_LAST_ID, request, error);

    // sucky hint
    error.set_leader_hint("127.0.0.1:0");
    service->serviceSpecificError(OpCode::GET_LAST_ID, request, error);

    // ok, fine, let it through
    service->reply(OpCode::GET_LAST_ID, request, expResponse);

    leaderRPC->call(OpCode::GET_LAST_ID, request, response);
    EXPECT_EQ(expResponse, response);
}

TEST_F(ClientLeaderRPCTest, handleServiceSpecificErrorSessionExpired) {
    Protocol::Client::Error error;
    error.set_error_code(Protocol::Client::Error::SESSION_EXPIRED);

    leaderRPC->eventLoop.exit();
    leaderRPC->eventLoopThread.join();

    EXPECT_DEATH({
            leaderRPC->eventLoopThread = std::thread(&Event::Loop::runForever,
                                                     &leaderRPC->eventLoop);
            init();
            service->serviceSpecificError(OpCode::GET_LAST_ID, request, error);
            leaderRPC->call(OpCode::GET_LAST_ID, request, response);
        },
        "Session expired");
}

// connect() tested adequately in tests for call()

TEST_F(ClientLeaderRPCTest, connectRandom) {
    // TODO(ongaro): This is hard to test without control of name resolution.
}

TEST_F(ClientLeaderRPCTest, connectHost) {
    init();
    leaderRPC->connectHost("127.0.0.2:0", leaderRPC->leaderSession);
    EXPECT_EQ("Closed session: Failed to connect socket to 127.0.0.2:0 "
              "(resolved to 127.0.0.2:0)",
              leaderRPC->leaderSession->toString());
}

} // namespace LogCabin::Client::<anonymous>
} // namespace LogCabin::Client
} // namespace LogCabin
