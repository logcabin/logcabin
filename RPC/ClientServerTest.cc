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

#include "include/Debug.h"
#include "RPC/ClientSession.h"
#include "RPC/Server.h"
#include "RPC/ServerRPC.h"
#include "RPC/Service.h"
#include "RPC/ThreadDispatchService.h"

namespace LogCabin {
namespace {

class EchoService : public RPC::Service {
    EchoService()
        : delayMicros(0)
    {
    }
    void handleRPC(RPC::ServerRPC serverRPC) {
        serverRPC.response = std::move(serverRPC.request);
        if (delayMicros != 0) {
            LOG(DBG, "Delaying response for %u microseconds", delayMicros);
            usleep(delayMicros);
            LOG(DBG, "Ok responding");
        }
        serverRPC.sendReply();
    }
    uint32_t delayMicros;
};

class RPCClientServerTest : public ::testing::Test {
    RPCClientServerTest()
        : clientEventLoop()
        , serverEventLoop()
        , clientEventLoopThread(&Event::Loop::runForever, &clientEventLoop)
        , serverEventLoopThread(&Event::Loop::runForever, &serverEventLoop)
        , address("127.0.0.1", 61023)
        , service()
        , threadDispatch(service, 1, 1)
        , server(serverEventLoop, address, 1024, threadDispatch)
        , clientSession(RPC::ClientSession::makeSession(
                            clientEventLoop, address, 1024))
    {
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
    EchoService service;
    RPC::ThreadDispatchService threadDispatch;
    RPC::Server server;
    std::shared_ptr<RPC::ClientSession> clientSession;
};

// Make sure the server can echo back messages.
TEST_F(RPCClientServerTest, echo) {
    for (uint32_t bufLen = 0; bufLen < 1024; ++bufLen) {
        char buf[bufLen];
        for (uint32_t i = 0; i < bufLen; ++i)
            buf[i] = char(i);
        RPC::ClientRPC rpc = clientSession->sendRequest(
                                        RPC::Buffer(buf, bufLen, NULL));
        RPC::Buffer reply = rpc.extractReply();
        EXPECT_EQ(bufLen, reply.getLength());
        EXPECT_EQ(0, memcmp(reply.getData(), buf, bufLen));
    }
}

// Test the RPC timeout (ping) mechanism.
// This test assumes TIMEOUT_MS is set to 100ms in ClientSession.
TEST_F(RPCClientServerTest, timeout) {
    service.delayMicros = 110 * 1000;

    // The server should not time out, since the serverEventLoopThread should
    // respond to pings.
    RPC::ClientRPC rpc = clientSession->sendRequest(RPC::Buffer());
    rpc.waitForReply();
    EXPECT_EQ("", rpc.getErrorMessage());

    // This time, if we don't let the server event loop run, the RPC should
    // time out.
    Event::Loop::Lock blockPings(serverEventLoop);
    RPC::ClientRPC rpc2 = clientSession->sendRequest(RPC::Buffer());
    rpc2.waitForReply();
    EXPECT_EQ("Server timed out", rpc2.getErrorMessage());

}


} // namespace LogCabin::<anonymous>
} // namespace LogCabin
