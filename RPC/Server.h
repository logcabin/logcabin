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

#include "RPC/MessageSocket.h"
#include "RPC/TCPListener.h"

#ifndef LOGCABIN_RPC_SERVER_H
#define LOGCABIN_RPC_SERVER_H

namespace LogCabin {

// forward declaration
namespace Event {
class Loop;
};

namespace RPC {

// forward declarations
class Address;
class Buffer;
class OpaqueServerRPC;
class Service;

/**
 * A Server listens for incoming RPCs over TCP connections and passes them off
 * to a Service. Servers can be created from any thread, but they will always
 * run on the thread running the Event::Loop.
 */
class Server {
  public:
    /**
     * Constructor. This object won't actually do anything until bind() is
     * called.
     * \param eventLoop
     *      Event::Loop that will be used to find out when the underlying
     *      socket may be read from or written to without blocking.
     * \param maxMessageLength
     *      The maximum number of bytes to allow per request/response. This
     *      exists to limit the amount of buffer space a single RPC can use.
     *      Attempting to send longer responses will PANIC; attempting to
     *      receive longer requests will disconnect the underlying socket.
     * \param service
     *      Handles RPCs from clients. Server will always call
     *      Service::handleRPC() from the Event::Loop thread, so this method
     *      must return quickly.
     */
    Server(Event::Loop& eventLoop,
           uint32_t maxMessageLength,
           Service& service);

    /**
     * Destructor. OpaqueServerRPC objects originating from this Server may be
     * kept around after this destructor returns; however, they won't actually
     * send replies anymore.
     */
    ~Server();

    /**
     * Listen on an address for new client connections. You can call this
     * multiple times to listen on multiple addresses. (But if you call this
     * twice with the same address, the second time will always throw an
     * error.)
     * This method is thread-safe.
     * \param listenAddress
     *      The TCP address on listen for new client connections.
     * \return
     *      An error message if this was not able to listen on the given
     *      address; the empty string otherwise.
     */
    std::string bind(const Address& listenAddress);

  private:

    /**
     * This listens for incoming TCP connections and creates new
     * ServerMessageSockets with new connections.
     */
    class ServerTCPListener : public TCPListener {
      public:
        explicit ServerTCPListener(Server* server);
        void handleNewConnection(int fd);
        /**
         * The Server which owns this object,
         * or NULL if the server is going away.
         */
        Server* server;

        // ServerTCPListener is not copyable.
        ServerTCPListener(const ServerTCPListener&) = delete;
        ServerTCPListener& operator=(const ServerTCPListener&) = delete;
    };

    /**
     * This is a MessageSocket with callbacks set up for Server.
     */
    class ServerMessageSocket : public MessageSocket {
      public:
        /**
         * Constructor.
         * \param server
         *      Server owning this socket.
         * \param fd
         *      A connected TCP socket.
         * \param socketsIndex
         *      The index into Server::sockets at which this object can be
         *      found.
         */
        ServerMessageSocket(Server* server, int fd, size_t socketsIndex);
        void onReceiveMessage(MessageId messageId, Buffer message);
        void onDisconnect();
        /**
         * Disconnect this socket. This drops the reference count on the
         * socket, so it will be closed soon after this method returns.
         * This may be called from any thread.
         */
        void close();
        /**
         * The Server which keeps a strong reference to this object, or NULL if
         * the server has gone away.
         */
        Server* server;
        /**
         * The index into Server::sockets at which this object can be found.
         */
        size_t socketsIndex;
        /**
         * A weak reference to this object, used to give OpaqueServerRPCs a way
         * to send their replies back on their originating socket.
         */
        std::weak_ptr<ServerMessageSocket> self;

        // ServerMessageSocket is not copyable.
        ServerMessageSocket(const ServerMessageSocket&) = delete;
        ServerMessageSocket& operator=(const ServerMessageSocket&) = delete;
    };

    /**
     * The event loop that is used for non-blocking I/O.
     */
    Event::Loop& eventLoop;

    /**
     * The maximum number of bytes to allow per request/response.
     */
    const uint32_t maxMessageLength;

    /**
     * The Service that will handle the OpaqueServerRPC objects that are
     * generated by this Server.
     */
    Service& service;

    /**
     * Every open ServerMessageSocket is referenced here so that it can be
     * cleaned up when this Server is destroyed. The lifetime of each socket
     * may slightly exceed the lifetime of the Server if it is being actively
     * used to send out a OpaqueServerRPC response when the Server is
     * destroyed.
     * This may only be accessed from the Event::Loop or while holding an
     * Event::Loop::Lock.
     */
    std::vector<std::shared_ptr<ServerMessageSocket>> sockets;

    /**
     * This listens for incoming TCP connections and accepts them.
     * This may only be accessed from the Event::Loop or while holding an
     * Event::Loop::Lock.
     */
    ServerTCPListener listener;

    /**
     * OpaqueServerRPC keeps a std::weak_ptr back to its originating
     * ServerMessageSocket.
     */
    friend class OpaqueServerRPC;

    // Server is non-copyable.
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
}; // class Server

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_SERVER_H */
