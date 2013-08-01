/* Copyright (c) 2010-2012 Stanford University
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
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/Endian.h"
#include "RPC/MessageSocket.h"

namespace LogCabin {
namespace RPC {

////////// MessageSocket::RawSocket //////////

MessageSocket::RawSocket::RawSocket(Event::Loop& eventLoop,
                                    int fd,
                                    MessageSocket& messageSocket)
    : Event::File(eventLoop, fd, Events::READABLE)
    , messageSocket(messageSocket)
    , closed(false)
{
}

MessageSocket::RawSocket::~RawSocket()
{
    close();
}

void
MessageSocket::RawSocket::close()
{
    Event::Loop::Lock lock(eventLoop);
    if (!closed) {
        setEvents(0);
        ::close(fd);
        closed = true;
    }
}

void
MessageSocket::RawSocket::setNotifyWritable(bool shouldNotify)
{
    Event::Loop::Lock lock(eventLoop);
    if (!closed) {
        if (shouldNotify)
            setEvents(Events::READABLE | Events::WRITABLE);
        else
            setEvents(Events::READABLE);
    }
}

void
MessageSocket::RawSocket::handleFileEvent(uint32_t events)
{
    assert(!closed);
    if (events & Events::READABLE) {
        messageSocket.readable();
        // return immediately in case readable() destroyed this object
        return;
    }
    if (events & Events::WRITABLE) {
        messageSocket.writable();
        // return immediately in case writable() destroyed this object
        return;
    }
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

////////// MessageSocket //////////

MessageSocket::MessageSocket(Event::Loop& eventLoop, int fd,
                             uint32_t maxMessageLength)
    : maxMessageLength(maxMessageLength)
    , inbound()
    , outboundQueueMutex()
    , outboundQueue()
    , socket(eventLoop, fd, *this)
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
    { // Place the message on the outbound queue.
        std::lock_guard<std::mutex> lock(outboundQueueMutex);
        outboundQueue.emplace(messageId, std::move(contents));
    }
    // Make sure the RawSocket is set up to call writable().
    socket.setNotifyWritable(true);
}

void
MessageSocket::readable()
{
    if (inbound.bytesRead < sizeof(Header)) {
        // Receiving header
        ssize_t bytesRead = read(
            reinterpret_cast<char*>(&inbound.header) + inbound.bytesRead,
            sizeof(Header) - inbound.bytesRead);
        if (bytesRead == -1)
            return; // disconnected, must return immediately
        inbound.bytesRead += bytesRead;
        if (inbound.bytesRead == sizeof(Header)) {
            // Transition to receiving data
            inbound.header.fromBigEndian();
            if (inbound.header.payloadLength > maxMessageLength) {
                WARNING("Dropping message that is too long to receive "
                        "(message is %u bytes, limit is %u bytes)",
                        inbound.header.payloadLength, maxMessageLength);
                socket.close();
                onDisconnect();
                return; // disconnected, must return immediately
            }
            inbound.message.setData(new char[inbound.header.payloadLength],
                                    inbound.header.payloadLength,
                                    Buffer::deleteArrayFn<char>);
        }
    }
    // Don't use 'else' here; we want to check this branch for two reasons:
    // First, if there is a header with a length of 0, the socket won't be
    // readable, but we still need to process the message. Second, most of the
    // time the header will arrive with at least some data. It makes sense to
    // go ahead and try a non-blocking read, rather than going back to the
    // event loop.
    if (inbound.bytesRead >= sizeof(Header)) {
        // Receiving data
        size_t payloadBytesRead = inbound.bytesRead - sizeof(Header);
        ssize_t bytesRead = read(
            static_cast<char*>(inbound.message.getData()) + payloadBytesRead,
            inbound.header.payloadLength - payloadBytesRead);
        if (bytesRead == -1)
            return; // disconnected, must return immediately
        inbound.bytesRead += bytesRead;
        if (inbound.bytesRead == (sizeof(Header) +
                                  inbound.header.payloadLength)) {
            // Transition to receiving header
            inbound.bytesRead = 0;
            onReceiveMessage(inbound.header.messageId,
                             std::move(inbound.message));
        }
    }
}

ssize_t
MessageSocket::read(void* buf, size_t maxBytes)
{
    ssize_t actual = recv(socket.fd, buf, maxBytes, MSG_DONTWAIT);
    if (actual > 0)
        return actual;
    if (actual == 0 || // peer performed orderly shutdown.
        errno == ECONNRESET || errno == ETIMEDOUT) {
        socket.close();
        // This must be the last line to touch this object, in case
        // onDisconnect() deletes this object.
        onDisconnect();
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    PANIC("Error while reading from socket: %s", strerror(errno));
}

void
MessageSocket::writable()
{
    // Get a pointer to the current outbound message.
    Outbound* outbound;
    {
        std::lock_guard<std::mutex> lock(outboundQueueMutex);
        if (outboundQueue.empty()) {
            // This shouldn't happen, but it's easy to deal with.
            socket.setNotifyWritable(false);
            return;
        }
        outbound = &outboundQueue.front();
    }

    // Use an iovec to send everything in one kernel call: one iov for the
    // header, another for the payload.
    enum { IOV_LEN = 2 };
    struct iovec iov[IOV_LEN];
    iov[0].iov_base = &outbound->header;
    iov[0].iov_len = sizeof(Header);
    iov[1].iov_base = outbound->message.getData();
    iov[1].iov_len = outbound->message.getLength();

    { // Skip the parts of the iovec that have already been sent.
        size_t bytesSent = outbound->bytesSent;
        for (uint32_t i = 0; i < IOV_LEN; ++i) {
            iov[i].iov_base = static_cast<char*>(iov[i].iov_base) + bytesSent;
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
    ssize_t bytesSent = sendmsg(socket.fd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (bytesSent >= 0) {
        // Sent successfully.
        outbound->bytesSent += bytesSent;
        if (outbound->bytesSent == (sizeof(Header) +
                                    outbound->message.getLength())) {
            // done with this message
            std::lock_guard<std::mutex> lock(outboundQueueMutex);
            outboundQueue.pop();
            if (outboundQueue.empty())
                socket.setNotifyWritable(false);
        }
        return;
    }
    // Wasn't able to send, try again later.
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return;
    // Connection closed; disconnect this end.
    if (errno == ECONNRESET || errno == EPIPE) {
        socket.close();
        // This must be the last line to touch this object, in case
        // onDisconnect() deletes this object.
        onDisconnect();
        return;
    }
    // Unexpected error.
    PANIC("Error while writing to socket %d: %s", socket.fd, strerror(errno));
}

} // namespace LogCabin::RPC
} // namespace LogCabin
