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

/**
 * \file
 * This is a simple end-to-end test of the basic RPC system.
 */

#include <string.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <thread>

#include "Core/Debug.h"
#include "Event/Timer.h"
#include "Protocol/Common.h"
#include "RPC/ClientSession.h"
#include "RPC/OpaqueClientRPC.h"
#include "RPC/OpaqueServer.h"
#include "RPC/OpaqueServerRPC.h"
#include "RPC/Service.h"
#include "RPC/ThreadDispatchService.h"

namespace LogCabin {
namespace {

class ReplyTimer : public Event::Timer {
    ReplyTimer(Event::Loop& eventLoop,
               RPC::OpaqueServerRPC serverRPC,
               uint32_t delayMicros)
        : Timer(eventLoop)
        , serverRPC(std::move(serverRPC))
    {
        schedule(delayMicros * 1000);
    }
    void handleTimerEvent() {
        VERBOSE("Ok responding");
        serverRPC.sendReply();
        delete this;
    }
    RPC::OpaqueServerRPC serverRPC;
};

class EchoServer : public RPC::OpaqueServer {
    EchoServer(Event::Loop& eventLoop, uint32_t maxMessageLength)
        : OpaqueServer(eventLoop, maxMessageLength)
        , delayMicros(0)
    {
    }
    void handleRPC(RPC::OpaqueServerRPC serverRPC) {
        serverRPC.response = std::move(serverRPC.request);
        VERBOSE("Delaying response for %u microseconds", delayMicros);
        new ReplyTimer(eventLoop, std::move(serverRPC), delayMicros);
    }
    uint32_t delayMicros;
};

class RPCClientServerTest : public ::testing::Test {
    RPCClientServerTest()
        : clientEventLoop()
        , serverEventLoop()
        , clientEventLoopThread(&Event::Loop::runForever, &clientEventLoop)
        , serverEventLoopThread(&Event::Loop::runForever, &serverEventLoop)
        , address("127.0.0.1", Protocol::Common::DEFAULT_PORT)
        , server(serverEventLoop, 1024)
        , clientSession()
    {
        EXPECT_EQ("", server.bind(address));
        clientSession = RPC::ClientSession::makeSession(
                            clientEventLoop, address, 1024);
    }
    ~RPCClientServerTest()
    {
        serverEventLoop.exit();
        clientEventLoop.exit();
        serverEventLoopThread.join();
        clientEventLoopThread.join();
    }

    Event::Loop clientEventLoop;
    Event::Loop serverEventLoop;
    std::thread clientEventLoopThread;
    std::thread serverEventLoopThread;
    RPC::Address address;
    EchoServer server;
    std::shared_ptr<RPC::ClientSession> clientSession;
};

// Make sure the server can echo back messages.
TEST_F(RPCClientServerTest, echo) {
    for (uint32_t bufLen = 0; bufLen < 1024; ++bufLen) {
        char buf[bufLen];
        for (uint32_t i = 0; i < bufLen; ++i)
            buf[i] = char(i);
        RPC::OpaqueClientRPC rpc = clientSession->sendRequest(
                                        RPC::Buffer(buf, bufLen, NULL));
        RPC::Buffer reply = rpc.extractReply();
        EXPECT_EQ(bufLen, reply.getLength());
        EXPECT_EQ(0, memcmp(reply.getData(), buf, bufLen));
    }
}

// Test the RPC timeout (ping) mechanism.
// This test assumes TIMEOUT_MS is set to 100ms in ClientSession.
TEST_F(RPCClientServerTest, timeout) {
    server.delayMicros = 110 * 1000;

    // The server should not time out, since the serverEventLoopThread should
    // respond to pings.
    RPC::OpaqueClientRPC rpc = clientSession->sendRequest(RPC::Buffer());
    rpc.waitForReply();
    EXPECT_EQ("", rpc.getErrorMessage());

    // This time, if we don't let the server event loop run, the RPC should
    // time out.
    Event::Loop::Lock blockPings(serverEventLoop);
    RPC::OpaqueClientRPC rpc2 = clientSession->sendRequest(RPC::Buffer());
    rpc2.waitForReply();
    EXPECT_EQ("Server 127.0.0.1:61023 (resolved to 127.0.0.1:61023) timed out",
              rpc2.getErrorMessage());

}


} // namespace LogCabin::<anonymous>
} // namespace LogCabin
