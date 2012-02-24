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

#include <gtest/gtest.h>
#include <string.h>
#include <thread>

#include "RPC/ClientSession.h"
#include "RPC/Server.h"
#include "RPC/ServerRPC.h"
#include "RPC/Service.h"

namespace LogCabin {
namespace {

class EchoService : public RPC::Service {
    void handleRPC(RPC::ServerRPC serverRPC) {
        serverRPC.response = std::move(serverRPC.request);
        serverRPC.sendReply();
    }
};

class RPCClientServerTest : public ::testing::Test {
    RPCClientServerTest()
        : eventLoop()
        , address("127.0.0.1", 61023)
        , service()
        , server(eventLoop, address, 1024, service)
        , clientSession(RPC::ClientSession::makeSession(
                                                  eventLoop, address, 1024))
    {
    }
    Event::Loop eventLoop;
    RPC::Address address;
    EchoService service;
    RPC::Server server;
    std::shared_ptr<RPC::ClientSession> clientSession;
};


TEST_F(RPCClientServerTest, echo) {
    std::thread thread(&Event::Loop::runForever, &eventLoop);

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

    eventLoop.exit();
    thread.join();
}


} // namespace LogCabin::<anonymous>
} // namespace LogCabin
