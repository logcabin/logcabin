/* Copyright (c) 2011 Stanford University
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

#include "DLogEvent.h"
#include "DLogRPC.h"

#ifndef LIBDLOGRPC_DLOGRPCINT_H
#define LIBDLOGRPC_DLOGRPCINT_H

namespace DLog {
namespace RPC {

/**
 * Server connection object that processing incoming messages, creates
 * message objects, and calls the requested service.
 */
class ServerConnection : private EventSocket
{
  public:
    /**
     * Constructor.
     */
    ServerConnection(Server &server,
                     EventLoop &eventLoop,
                     ConnectionId id,
                     int fd);
    virtual ~ServerConnection();
    /**
     * Process incomin data that is pending from the socket.
     */
    virtual void readCB();
    /**
     * Notify us that socket is ready to transmit more data.
     */
    virtual void writeCB();
    /**
     * Socket experienced a special event.
     */
    virtual void eventCB(EventMask events, int errnum);
    /**
     * Send a response through this connection.
     */
    virtual void sendResponse(Message& message);
  private:
    /// Connection state machine states.
    enum class ConnectionState {
        HEADER,
        BODY,
    };
    /// ConnectionId
    ConnectionId connectionId;
    /// Temporary message buffer.
    Message msg;
    /// Current connection state.
    ConnectionState connState;
    /// Pointer to server object.
    Server *server;
    ServerConnection(const ServerConnection&) = delete;
    ServerConnection& operator=(const ServerConnection&) = delete;
};

/**
 * Server listener object that accepts new incoming requests.
 */
class ServerListener : private EventListener
{
  public:
    /**
     * Constructor.
     */
    ServerListener(Server &rpcServer, EventLoop &eventLoop);
    virtual ~ServerListener();
    using EventListener::bind;
    /**
     * A new connection was requested.
     */
    virtual void acceptCB(int fd);
    /**
     * Process an error while trying to establish the listener socket.
     */
    virtual void errorCB(void);
  private:
    ConnectionId nextId;
    Server *server;
    EventLoop *loop;
    ServerListener(const ServerListener&) = delete;
    ServerListener& operator=(const ServerListener&) = delete;
};

} // namespace
} // namespace

#endif /* LIBDLOGRPC_DLOGRPCINT_H */
