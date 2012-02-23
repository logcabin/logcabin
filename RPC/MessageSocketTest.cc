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

#include <memory>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "include/Debug.h"
#include "RPC/MessageSocket.h"

namespace LogCabin {
namespace RPC {
namespace {

const char* payload = "abcdefghijklmnopqrstuvwxyz0123456789"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ+-";

class MyMessageSocket : public MessageSocket {
    MyMessageSocket(Event::Loop& eventLoop, int fd)
        : MessageSocket(eventLoop, fd, 64)
        , lastReceivedId(-1)
        , lastReceivedPayload()
        , disconnected(false)
    {
    }
    void onReceiveMessage(MessageId messageId, Buffer message) {
        lastReceivedId = messageId;
        lastReceivedPayload = std::move(message);
    }
    void onDisconnect() {
        EXPECT_FALSE(disconnected);
        disconnected = true;
    }
    MessageId lastReceivedId;
    Buffer lastReceivedPayload;
    bool disconnected;
};

class RPCMessageSocketTest : public ::testing::Test {
    RPCMessageSocketTest()
        : loop()
        , msgSocket()
        , remote(-1)
    {
        int socketPair[2];
        EXPECT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, socketPair));
        remote = socketPair[1];
        msgSocket.reset(new MyMessageSocket(loop, socketPair[0]));
    }
    ~RPCMessageSocketTest()
    {
        closeRemote();
    }
    void
    closeRemote()
    {
        if (remote >= 0) {
            EXPECT_EQ(0, close(remote));
            remote = -1;
        }
    }

    Event::Loop loop;
    std::unique_ptr<MyMessageSocket> msgSocket;
    int remote;
};

TEST_F(RPCMessageSocketTest, sendMessage) {
    EXPECT_DEATH(msgSocket->sendMessage(0, Buffer(NULL, ~0U, NULL)),
                 "too long to send");

    char hi[3];
    strncpy(hi, "hi", sizeof(hi));
    msgSocket->sendMessage(123, Buffer(hi, 3, NULL));
    ASSERT_EQ(1U, msgSocket->outboundQueue.size());
    MessageSocket::Outbound& outbound = msgSocket->outboundQueue.front();
    EXPECT_EQ(0U, outbound.bytesSent);
    outbound.header.fromBigEndian();
    EXPECT_EQ(123U, outbound.header.messageId);
    EXPECT_EQ(3U, outbound.header.payloadLength);
    EXPECT_EQ(hi, outbound.message.getData());
    EXPECT_EQ(3U, outbound.message.getLength());
}

TEST_F(RPCMessageSocketTest, readableSpurious) {
    msgSocket->readable();
    msgSocket->readable();
    EXPECT_EQ(0U, msgSocket->inbound.bytesRead);
    EXPECT_FALSE(msgSocket->disconnected);
}

TEST_F(RPCMessageSocketTest, readableSenderDisconnectInHeader) {
    closeRemote();
    msgSocket->readable();
    EXPECT_TRUE(msgSocket->disconnected);
    EXPECT_TRUE(msgSocket->socket.closed);
}

TEST_F(RPCMessageSocketTest, readableMessageTooLong) {
    MessageSocket::Header header;
    header.messageId = 0;
    header.payloadLength = 65;
    header.toBigEndian();
    EXPECT_EQ(ssize_t(sizeof(header)),
              send(remote, &header, sizeof(header), 0));
    msgSocket->readable();
    ASSERT_TRUE(msgSocket->disconnected);
}

TEST_F(RPCMessageSocketTest, readableEmptyPayload) {
    // This test exists to prevent a regression. Before, sending a message with
    // a length of 0 was not handled correctly.
    MessageSocket::Header header;
    header.messageId = 12;
    header.payloadLength = 0;
    header.toBigEndian();
    EXPECT_EQ(ssize_t(sizeof(header)),
              send(remote, &header, sizeof(header), 0));
    msgSocket->readable();
    ASSERT_FALSE(msgSocket->disconnected);
    EXPECT_EQ(0U, msgSocket->inbound.bytesRead);
    EXPECT_EQ(12U, msgSocket->lastReceivedId);
    EXPECT_EQ(0U, msgSocket->lastReceivedPayload.getLength());
}


TEST_F(RPCMessageSocketTest, readableSenderDisconnectInPayload) {
    MessageSocket::Header header;
    header.messageId = 0;
    header.payloadLength = 1;
    header.toBigEndian();
    EXPECT_EQ(ssize_t(sizeof(header)),
              send(remote, &header, sizeof(header), 0));
    msgSocket->readable();
    ASSERT_FALSE(msgSocket->disconnected);
    EXPECT_EQ(sizeof(header), msgSocket->inbound.bytesRead);
    closeRemote();
    msgSocket->readable();
    EXPECT_TRUE(msgSocket->disconnected);
    EXPECT_TRUE(msgSocket->socket.closed);
}

TEST_F(RPCMessageSocketTest, readableAllAtOnce) {
    MessageSocket::Header header;
    header.messageId = 0xdeadbeef8badf00d;
    header.payloadLength = 64;
    header.toBigEndian();
    char buf[sizeof(header) + 64];
    memcpy(buf, &header, sizeof(header));
    header.fromBigEndian();
    strncpy(buf + sizeof(header), payload, 64);
    EXPECT_EQ(ssize_t(sizeof(buf)), send(remote, buf, sizeof(buf), 0));
    // will do one read for the header
    msgSocket->readable();
    ASSERT_FALSE(msgSocket->disconnected);
    // and a second read for the data
    msgSocket->readable();
    ASSERT_FALSE(msgSocket->disconnected);
    EXPECT_EQ(header.messageId, msgSocket->lastReceivedId);
    EXPECT_EQ(payload,
              std::string(static_cast<const char*>(
                            msgSocket->lastReceivedPayload.getData()),
                          msgSocket->lastReceivedPayload.getLength()));

    EXPECT_EQ(1, send(remote, "x", 1, 0));
    msgSocket->readable();
    EXPECT_FALSE(msgSocket->disconnected);
    EXPECT_EQ(1U, msgSocket->inbound.bytesRead);
}

TEST_F(RPCMessageSocketTest, readableBytewise) {
    MessageSocket::Header header;
    header.messageId = 0xdeadbeef8badf00d;
    header.payloadLength = 64;
    header.toBigEndian();
    char buf[sizeof(header) + 64];
    memcpy(buf, &header, sizeof(header));
    header.fromBigEndian();
    strncpy(buf + sizeof(header), payload, 64);
    for (uint32_t i = 0; i < sizeof(buf); ++i) {
        EXPECT_EQ(1, send(remote, &buf[i], 1, 0));
        msgSocket->readable();
        ASSERT_FALSE(msgSocket->disconnected) << "Disconnected at byte " << i;
        msgSocket->readable(); // spurious
        ASSERT_FALSE(msgSocket->disconnected) << "Disconnected at byte " << i;
    }
    EXPECT_EQ(header.messageId, msgSocket->lastReceivedId);
    EXPECT_EQ(payload,
              std::string(static_cast<const char*>(
                            msgSocket->lastReceivedPayload.getData()),
                          msgSocket->lastReceivedPayload.getLength()));

    EXPECT_EQ(1, send(remote, "x", 1, 0));
    msgSocket->readable();
    EXPECT_FALSE(msgSocket->disconnected);
    EXPECT_EQ(1U, msgSocket->inbound.bytesRead);
}

TEST_F(RPCMessageSocketTest, writableSpurious) {
    msgSocket->writable();
}

TEST_F(RPCMessageSocketTest, writableDisconnect) {
    closeRemote();
    msgSocket->sendMessage(123,
                           Buffer(const_cast<char*>(payload), 64, NULL));
    ASSERT_EQ(1U, msgSocket->outboundQueue.size());
    ASSERT_FALSE(msgSocket->disconnected);
    msgSocket->writable();
    ASSERT_TRUE(msgSocket->disconnected);
}

TEST_F(RPCMessageSocketTest, writableNormal) {
    MessageSocket::Header header;
    header.messageId = 123;
    header.payloadLength = 64;
    header.toBigEndian();
    char expected[sizeof(header) + 64];
    memcpy(expected, &header, sizeof(header));
    strncpy(expected + sizeof(header), payload, 64);

    for (uint32_t i = 0; i < sizeof(MessageSocket::Header) + 64; ++i) {
        msgSocket->sendMessage(123,
                               Buffer(const_cast<char*>(payload), 64, NULL));
        ASSERT_EQ(1U, msgSocket->outboundQueue.size());
        msgSocket->outboundQueue.front().bytesSent = i;
        msgSocket->writable();
        ASSERT_FALSE(msgSocket->disconnected);
        ASSERT_EQ(0U, msgSocket->outboundQueue.size());
        char buf[sizeof(MessageSocket::Header) + 64 + 1];
        ASSERT_EQ(ssize_t(sizeof(buf)) - i - 1,
                  recv(remote, buf + i, sizeof(buf) - i, 0));
        ASSERT_EQ(0, memcmp(expected + i,
                            buf + i,
                            sizeof(buf) - i - 1))
            << "suffix of packet does not match from byte " << i;
    }
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
