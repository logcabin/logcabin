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
#include <queue>
#include <thread>

#include "Client/LeaderRPC.h"
#include "Core/ProtoBuf.h"
#include "Protocol/Client.h"
#include "RPC/OpaqueServerRPC.h"
#include "RPC/ProtoBuf.h"
#include "RPC/Server.h"
#include "RPC/Service.h"

namespace LogCabin {
namespace Client {
namespace {

using Protocol::Client::RequestHeaderPrefix;
using Protocol::Client::RequestHeaderVersion1;
using Protocol::Client::ResponseHeaderPrefix;
using Protocol::Client::ResponseHeaderVersion1;
using Protocol::Client::Status;
using ProtoBuf::ClientRPC::OpCode;

RPC::Buffer
copyBuffer(const RPC::Buffer& in)
{
    return RPC::Buffer(memcpy(new char[in.getLength()],
                              in.getData(),
                              in.getLength()),
                       in.getLength(),
                       RPC::Buffer::deleteArrayFn<char>);
}

RPC::Buffer
expectedRequest(uint8_t version,
                uint8_t opCode,
                const google::protobuf::Message& payload)
{
    RPC::Buffer buffer;
    RPC::ProtoBuf::serialize(payload, buffer,
                             sizeof(RequestHeaderVersion1));
    auto& requestHeader =
        *static_cast<RequestHeaderVersion1*>(buffer.getData());
    requestHeader.prefix.version = version;
    requestHeader.prefix.toBigEndian();
    requestHeader.opCode = opCode;
    requestHeader.toBigEndian();
    return buffer;
}

RPC::Buffer
successfulResponse(const google::protobuf::Message& payload)
{
    RPC::Buffer buffer;
    RPC::ProtoBuf::serialize(payload, buffer,
                             sizeof(ResponseHeaderVersion1));
    auto& responseHeader =
        *static_cast<ResponseHeaderVersion1*>(buffer.getData());
    responseHeader.prefix.status = Status::OK;
    responseHeader.prefix.toBigEndian();
    responseHeader.toBigEndian();
    return buffer;
}

RPC::Buffer
failedResponse(Status status,
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
    return buffer;
}

class MockServer : public RPC::Server {
    MockServer(Event::Loop& eventLoop, uint32_t maxMessageLength)
        : Server(eventLoop, maxMessageLength)
        , responseQueue()
        , closeNext(false)
    {
    }
    ~MockServer()
    {
        EXPECT_EQ(0U, responseQueue.size());
        EXPECT_FALSE(closeNext);
    }
    void handleRPC(RPC::OpaqueServerRPC serverRPC) {
        if (closeNext) {
            closeNext = false;
            serverRPC.closeSession();
            return;
        }
        ASSERT_GT(responseQueue.size(), 0U);
        RPC::Buffer expRequest = std::move(responseQueue.front().first);
        ASSERT_EQ(serverRPC.request.getLength(),
                  expRequest.getLength());
        ASSERT_EQ(0, memcmp(serverRPC.request.getData(),
                            expRequest.getData(),
                            expRequest.getLength()));
        serverRPC.response = std::move(responseQueue.front().second);
        responseQueue.pop();
        serverRPC.sendReply();
    }

    void expect(const RPC::Buffer& request,
                const RPC::Buffer& response)
    {
        responseQueue.emplace(copyBuffer(request),
                              copyBuffer(response));
    }

    std::queue<std::pair<RPC::Buffer, RPC::Buffer>> responseQueue;
    bool closeNext;
};

class ClientLeaderRPCTest : public ::testing::Test {
  public:
    ClientLeaderRPCTest()
        : serverEventLoop()
        , address("127.0.0.1", 61023)
        , server()
        , serverThread()
        , leaderRPC()
        , request()
        , response()
        , expResponse()
    {
        request.set_log_name("logName");
        expResponse.set_log_id(3);
    }
    ~ClientLeaderRPCTest()
    {
        serverEventLoop.exit();
        if (serverThread.joinable())
            serverThread.join();
    }

    void init() {
        server.reset(new MockServer(serverEventLoop, 1024 * 1024));
        EXPECT_EQ("", server->bind(address));
        serverThread = std::thread(&Event::Loop::runForever, &serverEventLoop);
        leaderRPC.reset(new LeaderRPC(address));
    }

    Event::Loop serverEventLoop;
    RPC::Address address;
    std::unique_ptr<MockServer> server;
    std::thread serverThread;
    std::unique_ptr<LeaderRPC> leaderRPC;
    ProtoBuf::ClientRPC::OpenLog::Request request;
    ProtoBuf::ClientRPC::OpenLog::Response response;
    ProtoBuf::ClientRPC::OpenLog::Response expResponse;
};

// constructor and destructor tested adequately in tests for call()

TEST_F(ClientLeaderRPCTest, callBasics) {
    init();
    server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                   successfulResponse(expResponse));
    leaderRPC->call(OpCode::OPEN_LOG,
                   request, response);
    EXPECT_EQ(expResponse, response);
}

TEST_F(ClientLeaderRPCTest, callServerNotListening) {
    init();
    server->closeNext = true;
    server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                   successfulResponse(expResponse));
    leaderRPC->call(OpCode::OPEN_LOG,
                   request, response);
    EXPECT_EQ(expResponse, response);
}

TEST_F(ClientLeaderRPCTest, callResponseTooShortForPrefix) {
    EXPECT_DEATH(
            init();
            server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                           RPC::Buffer());
            leaderRPC->call(OpCode::OPEN_LOG, request, response),
        "too short");
}

TEST_F(ClientLeaderRPCTest, callInvalidVersion) {
    EXPECT_DEATH(
            init();
            server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                           failedResponse(Status::INVALID_VERSION));
            leaderRPC->call(OpCode::OPEN_LOG, request, response),
        "client is too old");
}

TEST_F(ClientLeaderRPCTest, callResponseTooShortForHeader) {
    // It's actually impossible to test this right now,
    // since sizeof(ResponseHeaderPrefix) ==
    //       sizeof(ResponseHeaderVersion1)
}

TEST_F(ClientLeaderRPCTest, callOKButUnparsableResponse) {
    EXPECT_DEATH(
            init();
            server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                           failedResponse(Status::OK));
            leaderRPC->call(OpCode::OPEN_LOG, request, response),
        "Could not parse server response");
}

TEST_F(ClientLeaderRPCTest, callInvalidRequest) {
    EXPECT_DEATH(
            init();
            server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                           failedResponse(Status::INVALID_REQUEST));
            leaderRPC->call(OpCode::OPEN_LOG, request, response),
        "request.*invalid");
}

TEST_F(ClientLeaderRPCTest, callNotLeaderHint) {
    init();

    // no hint
    server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                   failedResponse(Status::NOT_LEADER));

    // sucky hint
    std::string badHint = "127.0.0.1:0";
    server->expect(
        expectedRequest(1, OpCode::OPEN_LOG, request),
        failedResponse(Status::NOT_LEADER,
                       RPC::Buffer(const_cast<char*>(badHint.c_str()),
                                   uint32_t(badHint.length()) + 1,
                                   NULL)));

    // ok, fine, let it through
    server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                   successfulResponse(expResponse));

    leaderRPC->call(OpCode::OPEN_LOG,
                   request, response);
    EXPECT_EQ(expResponse, response);
}

TEST_F(ClientLeaderRPCTest, callBadStatus) {
    Status badStatus;
    // type checking in C++11 is starting to get reasonable
    *reinterpret_cast<uint8_t*>(&badStatus) = 255;
    EXPECT_DEATH(
            init();
            server->expect(expectedRequest(1, OpCode::OPEN_LOG, request),
                           failedResponse(badStatus));
            leaderRPC->call(OpCode::OPEN_LOG, request, response),
        "Unknown status");
}

// connect*() tested adequately in tests for call()

} // namespace LogCabin::Client::<anonymous>
} // namespace LogCabin::Client
} // namespace LogCabin
