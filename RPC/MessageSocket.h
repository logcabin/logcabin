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

#include <deque>
#include <vector>

#include "Core/Mutex.h"
#include "Event/File.h"
#include "RPC/Buffer.h"

#ifndef LOGCABIN_RPC_MESSAGESOCKET_H
#define LOGCABIN_RPC_MESSAGESOCKET_H

namespace LogCabin {

// forward declaration
namespace Event {
class Loop;
}

namespace RPC {

/**
 * A MessageSocket is a message-oriented layer on top of a TCP connection.
 * It sends and receives discrete chunks of data identified by opaque IDs.
 * Higher layers can use this to build an RPC framework, both on the client
 * side and on the server side.
 *
 * On the wire, this adds a 12-byte header on all messages. The first 8 bytes,
 * encoded as an integer in big endian, are the message ID. The next 4 bytes,
 * encoded as an integer in big endian, are the number of bytes of data
 * following the header. Following the header, the data is sent as an opaque
 * binary string.
 *
 * TODO(ongaro): There are probably two things missing from this header. First,
 * it should have some mechanism (such as a version field) for changing the
 * header format later. Second, it should have a checksum, as the
 * TCP/IP/Ethernet checksums may not be sufficient.
 */
class MessageSocket {
  public:

    /**
     * An opaque identifier for a message.
     * For RPCs, clients can use this to pair up a response with its request,
     * and servers will want to reply with the same ID as the matching request.
     */
    typedef uint64_t MessageId;

    /**
     * Constructor.
     * \param eventLoop
     *      Event::Loop that will be used to find out when the socket is
     *      readable or writable.
     * \param fd
     *      Connected file descriptor for the socket. This object will close
     *      the file descriptor when it is disconnected.
     * \param maxMessageLength
     *      The maximum number of bytes of payload to allow per message. This
     *      exists to limit the amount of buffer space a single socket can use.
     *      Attempting to send longer messages will PANIC; attempting to
     *      receive longer messages will disconnect the socket.
     */
    explicit MessageSocket(Event::Loop& eventLoop,
                           int fd,
                           uint32_t maxMessageLength);

    /**
     * Destructor.
     */
    virtual ~MessageSocket();

    /**
     * Queue a message to be sent to the other side of this socket.
     * This method is safe to call from any thread.
     * \param messageId
     *      An opaque identifier for the message.
     * \param contents
     *      The data to send. This must be shorter than the maxMessageLength
     *      argument given to the constructor.
     */
    void sendMessage(MessageId messageId, Buffer contents);

    /**
     * This method is overridden by a subclass and invoked when a new message
     * is received. This method will be invoked by the main event loop on
     * whatever thread is running the Event::Loop.
     * \param messageId
     *      An opaque identifier for the message set by the sender.
     * \param contents
     *      The data received.
     */
    virtual void onReceiveMessage(MessageId messageId, Buffer contents) = 0;

    /**
     * This method is overridden by a subclass and invoked when the socket has
     * been closed. It is safe to destroy the MessageSocket during this call.
     * This method will be invoked by the main event loop at any time on
     * whatever thread is running the Event::Loop.
     */
    virtual void onDisconnect() = 0;

  private:

    /**
     * This class is an Event::File monitor that calls writable() when the
     * socket can be written to without blocking. When there are messages to be
     * sent out, it is set to EPOLLOUT|EPOLLONESHOT.
     *
     * Since ReceiveSocket is more efficient when one-shot is not used, and
     * SendSocket is more efficient when one-shot is used, the two are
     * monitored as separate Event::File objects.
     */
    struct SendSocket : public Event::File {
      public:
        SendSocket(Event::Loop& eventLoop, int fd,
                  MessageSocket& messageSocket);
        ~SendSocket();
        void handleFileEvent(int events);
      private:
        MessageSocket& messageSocket;
    };

    /**
     * This class is an Event::File monitor that calls readable() when the
     * socket can be read from without blocking. This is always set for EPOLLIN
     * events in a non-one-shot (persistent) manner.
     */
    struct ReceiveSocket : public Event::File {
      public:
        ReceiveSocket(Event::Loop& eventLoop, int fd,
                  MessageSocket& messageSocket);
        ~ReceiveSocket();
        void handleFileEvent(int events);
      private:
        MessageSocket& messageSocket;
    };

    /**
     * This is the header that precedes every message across the TCP socket.
     */
    struct Header {
        /**
         * Convert the contents to host order from big endian (how this header
         * should be transferred on the network).
         */
        void fromBigEndian();
        /**
         * Convert the contents to big endian (how this header should be
         * transferred on the network) from host order.
         */
        void toBigEndian();

        /**
         * A unique message ID assigned by the sender.
         */
        uint64_t messageId;

        /**
         * The length in bytes of the contents of the message, not including
         * this header.
         */
        uint32_t payloadLength;

    } __attribute__((packed));

    /**
     * This class stages a message while it is being received.
     */
    struct Inbound {
        /// Constructor.
        Inbound();
        /**
         * The number of bytes read for the message, including the header.
         */
        size_t bytesRead;
        /**
         * If bytesRead >= sizeof(header), the header has been fully received
         * and its fields are in host order. Otherwise, the header is still
         * being received here.
         */
        Header header;
        /**
         * The contents of the message (after the header) are staged here.
         */
        Buffer message;
    };

    /**
     * This class stages a message while it is being sent.
     */
    struct Outbound {
        /// Default constructor.
        Outbound();
        /// Move constructor.
        Outbound(Outbound&& other);
        /// Constructor.
        Outbound(MessageId messageId, Buffer message);
        /// Move assignment.
        Outbound& operator=(Outbound&& other);
        /**
         * The number of bytes already sent for this message, including the
         * header.
         */
        size_t bytesSent;
        /**
         * The message header, in big endian.
         */
        Header header;
        /**
         * The contents of the message (after the header).
         */
        Buffer message;
    };

    /**
     * Cleans up and calls onDisconnect() when the socket has an error.
     * Only called from event loop handlers.
     */
    void disconnect();

    /**
     * Called when the socket has data that can be read without blocking.
     */
    void readable();

    /**
     * Wrapper around recv(); used by readable().
     * \param buf
     *      Where to store the data received.
     * \param maxBytes
     *      The maximum number of bytes to receive and store into buf.
     * \return
     *      The number of bytes read (<= maxBytes), if successful.
     *      The value -1 indicates that the socket was disconnected, in which
     *      case the caller must be careful not to access this object and
     *      immediately return.
     */
    ssize_t read(void* buf, size_t maxBytes);

    /**
     * Called when the socket may be written to without blocking.
     */
    void writable();

    /**
     * The maximum number of bytes of payload to allow per message. This exists
     * to limit the amount of buffer space a single socket can use.
     */
    const uint32_t maxMessageLength;

    /**
     * The current message that is being received.
     */
    Inbound inbound;

    /**
     * Protects #outboundQueue only from concurrent modification.
     */
    Core::Mutex outboundQueueMutex;

    /**
     * A queue of messages waiting to be sent. The first one may be in the
     * middle of transmission, while the others have not yet started. This
     * queue is protected from concurrent modifications by #outboundQueueMutex.
     *
     * It's important that this remains a std::deque (or std::queue) because
     * writable() holds a pointer to the first element without the lock, while
     * sendMessage() may concurrently push onto the queue. std::deques are
     * guaranteed not to invalidate pointers while elements are pushed and
     * popped from the ends.
     */
    std::deque<Outbound> outboundQueue;

    /**
     * Notifies MessageSocket when the socket can be read from without
     * blocking.
     */
    ReceiveSocket receiveSocket;

    /**
     * Notifies MessageSocket when the socket can be transmitted on without
     * blocking.
     */
    SendSocket sendSocket;

    // MessageSocket is non-copyable.
    MessageSocket(const MessageSocket&) = delete;
    MessageSocket& operator=(const MessageSocket&) = delete;

}; // class MessageSocket

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_MESSAGESOCKET_H */
