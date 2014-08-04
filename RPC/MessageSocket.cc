/* Copyright (c) 2010-2014 Stanford University
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

#include <cassert>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/Endian.h"
#include "RPC/MessageSocket.h"

namespace LogCabin {
namespace RPC {

namespace {

/// Wrapper for dup().
int
dupOrPanic(int oldfd)
{
    int newfd = dup(oldfd);
    if (newfd < 0)
        PANIC("Failed to dup(%d): %s", oldfd, strerror(errno));
    return newfd;
}

} // anonymous namespace

////////// MessageSocket::SendSocket //////////

MessageSocket::SendSocket::SendSocket(Event::Loop& eventLoop,
                                      int fd,
                                      MessageSocket& messageSocket)
    : Event::File(eventLoop, fd, 0)
    , messageSocket(messageSocket)
{
    int flag = 1;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    if (r < 0) {
        WARNING("Could not set TCP_NODELAY flag on sending socket %d: %s",
                fd, strerror(errno));
    }
}

MessageSocket::SendSocket::~SendSocket()
{
}

void
MessageSocket::SendSocket::handleFileEvent(int events)
{
    messageSocket.writable();
}

////////// MessageSocket::ReceiveSocket //////////

MessageSocket::ReceiveSocket::ReceiveSocket(Event::Loop& eventLoop,
                                            int fd,
                                            MessageSocket& messageSocket)
    : Event::File(eventLoop, fd, EPOLLIN)
    , messageSocket(messageSocket)
{
    // I don't know that TCP_NODELAY has any effect if we're only reading from
    // this file descriptor, but I guess it can't hurt.
    int flag = 1;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    if (r < 0) {
        WARNING("Could not set TCP_NODELAY flag on receiving socket %d: %s",
                fd, strerror(errno));
    }
}

MessageSocket::ReceiveSocket::~ReceiveSocket()
{
}

void
MessageSocket::ReceiveSocket::handleFileEvent(int events)
{
    messageSocket.readable();
}

////////// MessageSocket::Header //////////

void
MessageSocket::Header::fromBigEndian()
{
    messageId = be64toh(messageId);
    payloadLength = be32toh(payloadLength);
}

void
MessageSocket::Header::toBigEndian()
{
    messageId = htobe64(messageId);
    payloadLength = htobe32(payloadLength);
}

////////// MessageSocket::Inbound //////////

MessageSocket::Inbound::Inbound()
    : bytesRead(0)
    , header()
    , message()
{
}

////////// MessageSocket::Outbound //////////

MessageSocket::Outbound::Outbound()
    : bytesSent(0)
    , header()
    , message()
{
}

MessageSocket::Outbound::Outbound(Outbound&& other)
    : bytesSent(other.bytesSent)
    , header(other.header)
    , message(std::move(other.message))
{
}

MessageSocket::Outbound::Outbound(MessageId messageId,
                                  Buffer message)
    : bytesSent(0)
    , header()
    , message(std::move(message))
{
    header.messageId = messageId;
    header.payloadLength = this->message.getLength();
    header.toBigEndian();
}

MessageSocket::Outbound&
MessageSocket::Outbound::operator=(Outbound&& other)
{
    bytesSent = other.bytesSent;
    header = other.header;
    message = std::move(other.message);
    return *this;
}

////////// MessageSocket //////////

MessageSocket::MessageSocket(Event::Loop& eventLoop, int fd,
                             uint32_t maxMessageLength)
    : maxMessageLength(maxMessageLength)
    , inbound()
    , outboundQueueMutex()
    , outboundQueue()
    , receiveSocket(eventLoop, dupOrPanic(fd), *this)
    , sendSocket(eventLoop, fd, *this)
{
}

MessageSocket::~MessageSocket()
{
}

void
MessageSocket::sendMessage(MessageId messageId, Buffer contents)
{
    // Check the message length.
    if (contents.getLength() > maxMessageLength) {
        PANIC("Message of length %u bytes is too long to send "
              "(limit is %u bytes)",
              contents.getLength(), maxMessageLength);
    }

    bool kick;
    { // Place the message on the outbound queue.
        std::lock_guard<Core::Mutex> lock(outboundQueueMutex);
        kick = outboundQueue.empty();
        outboundQueue.emplace_back(messageId, std::move(contents));
    }
    // Make sure the SendSocket is set up to call writable().
    if (kick)
        sendSocket.setEvents(EPOLLOUT|EPOLLONESHOT);
}

void
MessageSocket::disconnect()
{
    int r = close(receiveSocket.release());
    if (r != 0)
        PANIC("Could not close receive socket: %s", strerror(errno));
    r = close(sendSocket.release());
    if (r != 0)
        PANIC("Could not close send socket: %s", strerror(errno));
    // TODO(ongaro): to make it safe for epoll_wait to return  multiple events,
    // need to somehow queue the onDisconnect for later.
    onDisconnect();
}

void
MessageSocket::readable()
{
    if (receiveSocket.fd < 0)
        return;
    // Try to read data from the kernel until there is no more left.
    while (true) {
        if (inbound.bytesRead < sizeof(Header)) {
            // Receiving header
            ssize_t bytesRead = read(
                reinterpret_cast<char*>(&inbound.header) + inbound.bytesRead,
                sizeof(Header) - inbound.bytesRead);
            if (bytesRead == -1) {
                disconnect();
                return;
            }
            inbound.bytesRead += bytesRead;
            if (inbound.bytesRead < sizeof(Header))
                return;
            // Transition to receiving data
            inbound.header.fromBigEndian();
            if (inbound.header.payloadLength > maxMessageLength) {
                WARNING("Dropping message that is too long to receive "
                        "(message is %u bytes, limit is %u bytes)",
                        inbound.header.payloadLength, maxMessageLength);
                disconnect();
                return;
            }
            inbound.message.setData(new char[inbound.header.payloadLength],
                                    inbound.header.payloadLength,
                                    Buffer::deleteArrayFn<char>);
        }
        // Don't use 'else' here; we want to check this branch for two reasons:
        // First, if there is a header with a length of 0, the socket won't be
        // readable, but we still need to process the message. Second, most of
        // the time the header will arrive with at least some data. It makes
        // sense to go ahead and try a non-blocking read, rather than going
        // back to the event loop.
        if (inbound.bytesRead >= sizeof(Header)) {
            // Receiving data
            size_t payloadBytesRead = inbound.bytesRead - sizeof(Header);
            ssize_t bytesRead = read(
                (static_cast<char*>(inbound.message.getData()) +
                 payloadBytesRead),
                inbound.header.payloadLength - payloadBytesRead);
            if (bytesRead == -1) {
                disconnect();
                return;
            }
            inbound.bytesRead += bytesRead;
            if (inbound.bytesRead < (sizeof(Header) +
                                     inbound.header.payloadLength)) {
                return;
            }
            onReceiveMessage(inbound.header.messageId,
                             std::move(inbound.message));
            // Transition to receiving header
            inbound.bytesRead = 0;
        }
    }
}

ssize_t
MessageSocket::read(void* buf, size_t maxBytes)
{
    ssize_t actual = recv(receiveSocket.fd, buf, maxBytes, MSG_DONTWAIT);
    if (actual > 0)
        return actual;
    if (actual == 0 || // peer performed orderly shutdown.
        errno == ECONNRESET || errno == ETIMEDOUT) {
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    PANIC("Error while reading from socket: %s", strerror(errno));
}

void
MessageSocket::writable()
{
    if (receiveSocket.fd < 0)
        return;
    // Each iteration of this loop tries to write one message
    // from outboundQueue.
    while (true) {

        // Get the next outbound message.
        Outbound outbound;
        int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
        {
            std::lock_guard<Core::Mutex> lock(outboundQueueMutex);
            if (outboundQueue.empty())
                return;
            outbound = std::move(outboundQueue.front());
            outboundQueue.pop_front();
            if (!outboundQueue.empty())
                flags |= MSG_MORE;
        }

        // Use an iovec to send everything in one kernel call: one iov for the
        // header, another for the payload.
        enum { IOV_LEN = 2 };
        struct iovec iov[IOV_LEN];
        iov[0].iov_base = &outbound.header;
        iov[0].iov_len = sizeof(Header);
        iov[1].iov_base = outbound.message.getData();
        iov[1].iov_len = outbound.message.getLength();

        { // Skip the parts of the iovec that have already been sent.
            size_t bytesSent = outbound.bytesSent;
            for (uint32_t i = 0; i < IOV_LEN; ++i) {
                iov[i].iov_base = (static_cast<char*>(iov[i].iov_base) +
                                   bytesSent);
                if (bytesSent < iov[i].iov_len) {
                    iov[i].iov_len -= bytesSent;
                    bytesSent = 0;
                    break;
                } else {
                    bytesSent -= iov[i].iov_len;
                    iov[i].iov_len = 0;
                }
            }
        }

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = IOV_LEN;

        // Do the actual send
        ssize_t bytesSent = sendmsg(sendSocket.fd, &msg, flags);
        if (bytesSent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // Wasn't able to send, try again later.
                bytesSent = 0;
            } else if (errno == ECONNRESET || errno == EPIPE) {
                // Connection closed; disconnect this end.
                // This must be the last line to touch this object, in case
                // onDisconnect() deletes this object.
                disconnect();
                return;
            } else {
                // Unexpected error.
                PANIC("Error while writing to socket %d: %s",
                      sendSocket.fd, strerror(errno));
            }
        }

        // Sent successfully.
        outbound.bytesSent += bytesSent;
        if (outbound.bytesSent != (sizeof(Header) +
                                   outbound.message.getLength())) {
            sendSocket.setEvents(EPOLLOUT|EPOLLONESHOT);
            std::unique_lock<Core::Mutex> lockGuard(outboundQueueMutex);
            outboundQueue.emplace_front(std::move(outbound));
            return;
        }
    }
}

} // namespace LogCabin::RPC
} // namespace LogCabin
