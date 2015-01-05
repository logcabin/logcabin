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

#include "Core/Debug.h"
#include "Event/Loop.h"
#include "Protocol/Common.h"
#include "RPC/OpaqueServer.h"
#include "RPC/OpaqueServerRPC.h"

namespace LogCabin {
namespace RPC {
namespace {

class MyServer : public OpaqueServer {
    MyServer(Event::Loop& eventLoop, uint32_t maxMessageLength)
        : OpaqueServer(eventLoop, maxMessageLength)
        , lastRPC()
    {
    }
    void handleRPC(OpaqueServerRPC serverRPC) {
        lastRPC.reset(new OpaqueServerRPC(std::move(serverRPC)));
    }
    std::unique_ptr<OpaqueServerRPC> lastRPC;
};

class RPCOpaqueServerTest : public ::testing::Test {
    RPCOpaqueServerTest()
        : loop()
        , address("127.0.0.1", Protocol::Common::DEFAULT_PORT)
        , server(loop, 1024)
        , fd1(-1)
        , fd2(-1)
    {
        address.refresh(RPC::Address::TimePoint::max());
        EXPECT_EQ("", server.bind(address));
        int fds[2];
        EXPECT_EQ(0, pipe(fds));
        fd1 = fds[0];
        fd2 = fds[1];
    }
    ~RPCOpaqueServerTest() {
        if (fd1 != -1)
            EXPECT_EQ(0, close(fd1));
        if (fd2 != -1)
            EXPECT_EQ(0, close(fd2));
    }
    Event::Loop loop;
    Address address;
    MyServer server;
    int fd1;
    int fd2;
};

TEST_F(RPCOpaqueServerTest, TCPListener_handleNewConnection) {
    server.listener.handleNewConnection(fd1);
    fd1 = -1;
    ASSERT_EQ(1U, server.sockets.size());
    OpaqueServer::ServerMessageSocket& socket = *server.sockets.at(0);
    EXPECT_EQ(&server, socket.server);
    EXPECT_EQ(0U, socket.socketsIndex);
    EXPECT_FALSE(socket.self.expired());

    server.listener.server = NULL;
    server.listener.handleNewConnection(fd2);
    fd2 = -1;
    EXPECT_EQ(1U, server.sockets.size());
}

TEST_F(RPCOpaqueServerTest, MessageSocket_onReceiveMessage) {
    server.listener.handleNewConnection(fd1);
    fd1 = -1;
    OpaqueServer::ServerMessageSocket& socket = *server.sockets.at(0);
    socket.onReceiveMessage(1, Buffer(NULL, 3, NULL));
    ASSERT_TRUE(server.lastRPC.get());
    EXPECT_EQ(3U, server.lastRPC->request.getLength());
    EXPECT_EQ(0U, server.lastRPC->response.getLength());
    EXPECT_EQ(&socket, server.lastRPC->messageSocket.lock().get());
    EXPECT_EQ(1U, server.lastRPC->messageId);
}

TEST_F(RPCOpaqueServerTest, MessageSocket_onReceiveMessage_ping) {
    server.listener.handleNewConnection(fd1);
    fd1 = -1;
    OpaqueServer::ServerMessageSocket& socket = *server.sockets.at(0);
    socket.onReceiveMessage(0, Buffer());
    ASSERT_FALSE(server.lastRPC);
    EXPECT_EQ(1U, socket.outboundQueue.size());
}

TEST_F(RPCOpaqueServerTest, MessageSocket_onDisconnect) {
    server.listener.handleNewConnection(fd1);
    fd1 = -1;
    server.listener.handleNewConnection(fd2);
    fd2 = -1;
    EXPECT_EQ(2U, server.sockets.size());
    std::shared_ptr<OpaqueServer::ServerMessageSocket> socket =
        server.sockets.at(0);
    socket->onDisconnect();
    EXPECT_EQ(1U, server.sockets.size());
    EXPECT_EQ(0U, server.sockets.at(0)->socketsIndex);
    EXPECT_TRUE(socket->server == NULL);
    socket->close();
    EXPECT_EQ(1U, server.sockets.size());
}

TEST_F(RPCOpaqueServerTest, MessageSocket_close) {
    // tested by onDisconnect
}

TEST_F(RPCOpaqueServerTest, constructor) {
    // tested sufficiently in other tests
}

TEST_F(RPCOpaqueServerTest, destructor) {
    // difficult to test
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
