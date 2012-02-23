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

#include <mutex>
#include <queue>
#include <vector>

#include "Event/Loop.h"
#include "Event/File.h"
#include "RPC/Buffer.h"

#ifndef LOGCABIN_RPC_MESSAGESOCKET_H
#define LOGCABIN_RPC_MESSAGESOCKET_H

namespace LogCabin {
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
     * This class is an Event::File monitor that calls readable() and
     * writable() when the socket can be read from or written to without
     * blocking. It's not meant to be a real abstraction.
     *
     * It is normally set to watch for READABLE events. When there are messages
     * to be sent out, it is also set to watch for WRITABLE events. Once it's
     * been closed, it is set to watch for no events.
     */
    class RawSocket : private Event::File {
      public:
        /**
         * Constructor.
         * \param eventLoop
         *      Event::Loop that will be used to find out when the socket is
         *      readable or writable.
         * \param fd
         *      Connected file descriptor for the socket. This object will
         *      close the file descriptor when it is disconnected.
         * \param messageSocket
         *      The MessageSocket to notify.
         */
        RawSocket(Event::Loop& eventLoop, int fd,
                  MessageSocket& messageSocket);
        /// Destructor.
        ~RawSocket();

        /**
         * Close the socket if it is not already closed.
         * This may be called from any thread (it uses an Event::Loop::Lock
         * internally).
         */
        void close();

        /**
         * Set whether the MessageSocket should be notified when writing to the
         * socket wouldn't block. This may be called from any thread (it uses
         * an Event::Loop::Lock internally).
         * \param shouldNotify
         *      True if the MessageSocket is interested in Events::Writable
         *      notifications, false otherwise.
         */
        void setNotifyWritable(bool shouldNotify);

        // Expose 'fd' from Event::File.
        // This should only be accessed from the event loop thread.
        using Event::File::fd;

      private:
        void handleFileEvent(uint32_t events);
        /// The MessageSocket to notify.
        MessageSocket& messageSocket;
        /**
         * Set to true if the socket has been closed.
         * This can only be accessed while holding an Event::Loop::Lock.
         */
        bool closed;
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
        /// Constructor.
        Outbound(MessageId messageId, Buffer message);
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
    std::mutex outboundQueueMutex;

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
    std::queue<Outbound> outboundQueue;

    /**
     * Notifies MessageSocket when the socket can be read from or written to
     * without blocking.
     */
    RawSocket socket;

    // MessageSocket is non-copyable.
    MessageSocket(const MessageSocket&) = delete;
    MessageSocket& operator=(const MessageSocket&) = delete;

}; // class MessageSocket

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_MESSAGESOCKET_H */
