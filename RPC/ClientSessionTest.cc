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
#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 5
#include <atomic>
#else
#include <cstdatomic>
#endif

#include "Core/Debug.h"
#include "Event/Timer.h"
#include "RPC/ClientSession.h"

namespace LogCabin {
namespace RPC {
namespace {

class RPCClientSessionTest : public ::testing::Test {
    RPCClientSessionTest()
        : eventLoop()
        , eventLoopThread(&Event::Loop::runForever, &eventLoop)
        , session()
        , remote(-1)
    {
        Address address("127.0.0.1", 0);
        session = ClientSession::makeSession(eventLoop, address, 1024);
        int socketPair[2];
        EXPECT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, socketPair));
        remote = socketPair[1];
        session->errorMessage.clear();
        session->messageSocket.reset(
            new ClientSession::ClientMessageSocket(
                *session, socketPair[0], 1024));
    }

    ~RPCClientSessionTest()
    {
        session.reset();
        eventLoop.exit();
        eventLoopThread.join();
        EXPECT_EQ(0, close(remote));
    }

    Event::Loop eventLoop;
    std::thread eventLoopThread;
    std::shared_ptr<ClientSession> session;
    int remote;
};

std::string
str(const Buffer& buffer)
{
    return std::string(static_cast<const char*>(buffer.getData()),
                       buffer.getLength());
}

Buffer
buf(const char* stringLiteral)
{
    return Buffer(const_cast<char*>(stringLiteral),
                  static_cast<uint32_t>(strlen(stringLiteral)),
                  NULL);
}

TEST_F(RPCClientSessionTest, onReceiveMessage) {
    session->numActiveRPCs = 1;

    // Unexpected
    session->messageSocket->onReceiveMessage(1, buf("a"));

    // Normal
    session->timer.schedule(1);
    session->responses[1] = new ClientSession::Response();
    session->messageSocket->onReceiveMessage(1, buf("b"));
    EXPECT_TRUE(session->responses[1]->ready);
    EXPECT_EQ("b", str(session->responses[1]->reply));
    EXPECT_EQ(0U, session->numActiveRPCs);
    EXPECT_FALSE(session->timer.isScheduled());

    // Already ready
    session->messageSocket->onReceiveMessage(1, buf("c"));
    EXPECT_EQ("b", str(session->responses[1]->reply));
    EXPECT_EQ(0U, session->numActiveRPCs);
}

TEST_F(RPCClientSessionTest, onReceiveMessage_ping) {
    // spurious
    session->messageSocket->onReceiveMessage(0, Buffer());

    // ping requested
    session->numActiveRPCs = 1;
    session->activePing = true;
    session->messageSocket->onReceiveMessage(0, Buffer());
    session->numActiveRPCs = 0;
    EXPECT_FALSE(session->activePing);
    EXPECT_TRUE(session->timer.isScheduled());
}

TEST_F(RPCClientSessionTest, onDisconnect) {
    session->messageSocket->onDisconnect();
    EXPECT_EQ("Disconnected from server", session->errorMessage);
}

TEST_F(RPCClientSessionTest, handleTimerEvent) {
    // spurious
    std::unique_ptr<ClientSession::ClientMessageSocket> oldMessageSocket;
    std::swap(oldMessageSocket, session->messageSocket);
    session->timer.handleTimerEvent();
    std::swap(oldMessageSocket, session->messageSocket);
    session->timer.handleTimerEvent();
    // make sure no actions were taken:
    EXPECT_FALSE(session->timer.isScheduled());
    EXPECT_EQ("", session->errorMessage);

    // need to send ping
    session->numActiveRPCs = 1;
    session->timer.handleTimerEvent();
    EXPECT_TRUE(session->activePing);
    char b;
    EXPECT_EQ(1U, read(remote, &b, 1));
    EXPECT_TRUE(session->timer.isScheduled());

    // need to time out session
    session->numActiveRPCs = 1;
    session->timer.handleTimerEvent();
    EXPECT_EQ("Server timed out", session->errorMessage);
    session->numActiveRPCs = 0;
}

TEST_F(RPCClientSessionTest, constructor) {
    auto session2 = ClientSession::makeSession(eventLoop,
                                               Address("127.0.0.1", 0),
                                               1024);
    EXPECT_EQ("Failed to connect socket to 127.0.0.1:0 "
              "(resolved to 127.0.0.1:0)",
              session2->errorMessage);
    EXPECT_FALSE(session2->messageSocket);
}

TEST_F(RPCClientSessionTest, makeSession) {
    EXPECT_EQ(session.get(), session->self.lock().get());
}

TEST_F(RPCClientSessionTest, destructor) {
    // nothing visible to test
}

TEST_F(RPCClientSessionTest, sendRequest) {
    EXPECT_EQ(1U, session->nextMessageId);
    session->activePing = true;
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    EXPECT_EQ(1U, session->numActiveRPCs);
    EXPECT_FALSE(session->activePing);
    EXPECT_TRUE(session->timer.isScheduled());
    EXPECT_EQ(session, rpc.session);
    EXPECT_EQ(1U, rpc.responseToken);
    EXPECT_FALSE(rpc.ready);
    EXPECT_EQ(2U, session->nextMessageId);
    auto it = session->responses.find(1);
    ASSERT_TRUE(it != session->responses.end());
    ClientSession::Response& response = *it->second;
    EXPECT_FALSE(response.ready);
}

TEST_F(RPCClientSessionTest, getErrorMessage) {
    EXPECT_EQ("", session->getErrorMessage());
    session->errorMessage = "x";
    EXPECT_EQ("x", session->getErrorMessage());
}

TEST_F(RPCClientSessionTest, cancel) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    EXPECT_EQ(1U, session->numActiveRPCs);
    rpc.cancel();
    EXPECT_EQ(0U, session->numActiveRPCs);
    EXPECT_TRUE(rpc.ready);
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ(0U, rpc.reply.getLength());
    EXPECT_EQ("RPC canceled by user", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, updateNotReady) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    rpc.update();
    EXPECT_FALSE(rpc.ready);
    EXPECT_EQ(0U, rpc.reply.getLength());
    EXPECT_EQ("", rpc.errorMessage);
    EXPECT_EQ(1U, session->responses.size());
}

TEST_F(RPCClientSessionTest, updateReady) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    auto it = session->responses.find(1);
    ASSERT_TRUE(it != session->responses.end());
    ClientSession::Response& response = *it->second;
    response.ready = true;
    response.reply = buf("bye");
    rpc.update();
    EXPECT_TRUE(rpc.ready);
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("bye", str(rpc.reply));
    EXPECT_EQ("", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, updateError) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    session->errorMessage = "some error";
    rpc.update();
    EXPECT_TRUE(rpc.ready);
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("", str(rpc.reply));
    EXPECT_EQ("some error", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, waitNotReady) {
    // It's hard to test this one since it'll block.
}

TEST_F(RPCClientSessionTest, waitReady) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    auto it = session->responses.find(1);
    ASSERT_TRUE(it != session->responses.end());
    ClientSession::Response& response = *it->second;
    response.ready = true;
    response.reply = buf("bye");
    rpc.waitForReply();
    EXPECT_TRUE(rpc.ready);
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("bye", str(rpc.reply));
    EXPECT_EQ("", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, waitError) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    session->errorMessage = "some error";
    rpc.waitForReply();
    EXPECT_TRUE(rpc.ready);
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("", str(rpc.reply));
    EXPECT_EQ("some error", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
