/* Copyright (c) 2012-2014 Stanford University
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
    session->timer.schedule(1000000000);
    session->responses[1] = new ClientSession::Response();
    session->messageSocket->onReceiveMessage(1, buf("b"));
    EXPECT_EQ(ClientSession::Response::HAS_REPLY,
              session->responses[1]->status);
    EXPECT_EQ("b", str(session->responses[1]->reply));
    EXPECT_EQ(0U, session->numActiveRPCs);
    EXPECT_FALSE(session->timer.isScheduled());

    // Already ready
    LogCabin::Core::Debug::setLogPolicy({{"", "ERROR"}});
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
    EXPECT_EQ("Disconnected from server 127.0.0.1 (resolved to 127.0.0.1:0)",
              session->errorMessage);
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
    EXPECT_EQ("Server 127.0.0.1 (resolved to 127.0.0.1:0) timed out",
              session->errorMessage);
    session->numActiveRPCs = 0;
}

TEST_F(RPCClientSessionTest, constructor) {
    auto session2 = ClientSession::makeSession(eventLoop,
                                               Address("127.0.0.1", 0),
                                               1024);
    EXPECT_EQ("127.0.0.1 (resolved to 127.0.0.1:0)",
              session2->address.toString());
    EXPECT_EQ("Failed to connect socket to 127.0.0.1 "
              "(resolved to 127.0.0.1:0)",
              session2->errorMessage);
    EXPECT_EQ("Closed session: Failed to connect socket to 127.0.0.1 "
              "(resolved to 127.0.0.1:0)",
              session2->toString());
    EXPECT_FALSE(session2->messageSocket);

    auto session3 = ClientSession::makeSession(eventLoop,
                                               Address("i n v a l i d", 0),
                                               1024);
    EXPECT_EQ("Failed to resolve i n v a l i d (resolved to Unspecified)",
              session3->errorMessage);
    EXPECT_EQ("Closed session: Failed to resolve i n v a l i d "
              "(resolved to Unspecified)",
              session3->toString());
    EXPECT_FALSE(session3->messageSocket);
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
    EXPECT_EQ(OpaqueClientRPC::Status::NOT_READY, rpc.getStatus());
    EXPECT_EQ(2U, session->nextMessageId);
    auto it = session->responses.find(1);
    ASSERT_TRUE(it != session->responses.end());
    ClientSession::Response& response = *it->second;
    EXPECT_EQ(ClientSession::Response::WAITING,
              response.status);
}

TEST_F(RPCClientSessionTest, getErrorMessage) {
    EXPECT_EQ("", session->getErrorMessage());
    session->errorMessage = "x";
    EXPECT_EQ("x", session->getErrorMessage());
}

TEST_F(RPCClientSessionTest, toString) {
    EXPECT_EQ("Active session to 127.0.0.1 (resolved to 127.0.0.1:0)",
              session->toString());
}

TEST_F(RPCClientSessionTest, cancel) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    EXPECT_EQ(1U, session->numActiveRPCs);
    rpc.cancel();
    rpc.cancel(); // intentionally duplicated
    EXPECT_EQ(0U, session->numActiveRPCs);
    EXPECT_EQ(OpaqueClientRPC::Status::CANCELED, rpc.getStatus());
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ(0U, rpc.reply.getLength());
    EXPECT_EQ("RPC canceled by user", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());

    // Cancel while there's a waiter is tested below in
    // waitCanceledWhileWaiting.
}

TEST_F(RPCClientSessionTest, updateCanceled) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    rpc.cancel();
    rpc.update();
    EXPECT_EQ(OpaqueClientRPC::Status::CANCELED, rpc.getStatus());
    EXPECT_EQ(0U, rpc.reply.getLength());
    EXPECT_EQ("RPC canceled by user", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, updateNotReady) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    rpc.update();
    EXPECT_EQ(OpaqueClientRPC::Status::NOT_READY, rpc.getStatus());
    EXPECT_EQ(0U, rpc.reply.getLength());
    EXPECT_EQ("", rpc.errorMessage);
    EXPECT_EQ(1U, session->responses.size());
}

TEST_F(RPCClientSessionTest, updateReady) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    auto it = session->responses.find(1);
    ASSERT_TRUE(it != session->responses.end());
    ClientSession::Response& response = *it->second;
    response.status = ClientSession::Response::HAS_REPLY;
    response.reply = buf("bye");
    rpc.update();
    EXPECT_EQ(OpaqueClientRPC::Status::OK, rpc.getStatus());
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("bye", str(rpc.reply));
    EXPECT_EQ("", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, updateError) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    session->errorMessage = "some error";
    rpc.update();
    EXPECT_EQ(OpaqueClientRPC::Status::ERROR, rpc.getStatus());
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("", str(rpc.reply));
    EXPECT_EQ("some error", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, waitNotReady) {
    // It's hard to test this one since it'll block.
    // TODO(ongaro): Use Core/ConditionVariable
}

TEST_F(RPCClientSessionTest, waitCanceled) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    rpc.cancel();
    rpc.waitForReply();
    EXPECT_EQ(OpaqueClientRPC::Status::CANCELED, rpc.getStatus());
}

TEST_F(RPCClientSessionTest, waitCanceledWhileWaiting) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    {
        ClientSession::Response& response = *session->responses.at(1);
        response.ready.callback = std::bind(&OpaqueClientRPC::cancel, &rpc);
        rpc.waitForReply();
    }
    EXPECT_EQ(OpaqueClientRPC::Status::CANCELED, rpc.getStatus());
    EXPECT_EQ("RPC canceled by user", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, waitReady) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    auto it = session->responses.find(1);
    ASSERT_TRUE(it != session->responses.end());
    ClientSession::Response& response = *it->second;
    response.status = ClientSession::Response::HAS_REPLY;
    response.reply = buf("bye");
    rpc.waitForReply();
    EXPECT_EQ(OpaqueClientRPC::Status::OK, rpc.getStatus());
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("bye", str(rpc.reply));
    EXPECT_EQ("", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

TEST_F(RPCClientSessionTest, waitError) {
    OpaqueClientRPC rpc = session->sendRequest(buf("hi"));
    session->errorMessage = "some error";
    rpc.waitForReply();
    EXPECT_EQ(OpaqueClientRPC::Status::ERROR, rpc.getStatus());
    EXPECT_FALSE(rpc.session);
    EXPECT_EQ("", str(rpc.reply));
    EXPECT_EQ("some error", rpc.errorMessage);
    EXPECT_EQ(0U, session->responses.size());
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
