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

#include "RPC/Buffer.h"
#include "RPC/MessageSocket.h"
#include "RPC/Server.h"

#ifndef LOGCABIN_RPC_SERVERRPC_H
#define LOGCABIN_RPC_SERVERRPC_H

namespace LogCabin {
namespace RPC {

/**
 * This class represents the server side of a remote procedure call.
 * A Server returns an instance when an RPC is initiated. This is used to send
 * the reply.
 *
 * This class may be used from any thread, but each object is meant to be
 * accessed by only one thread at a time.
 */
class ServerRPC {
    /**
     * Constructor for ServerRPC. This is called by Server.
     * \param messageSocket
     *      The socket on which to send the reply.
     * \param messageId
     *      The message ID received with the request.
     * \param request
     *      The RPC request received from the client.
     */
    ServerRPC(std::weak_ptr<Server::ServerMessageSocket> messageSocket,
              MessageSocket::MessageId messageId,
              Buffer request);

  public:
    /**
     * Move constructor.
     */
    ServerRPC(ServerRPC&& other);

    /**
     * Destructor.
     */
    ~ServerRPC();

    /**
     * Move assignment.
     */
    ServerRPC& operator=(ServerRPC&& other);

    /**
     * Close the session on which this request originated.
     * This is an impolite thing to do to a client but can be useful
     * occasionally, for example for testing.
     */
    void closeSession();

    /**
     * Send the response back to the client.
     * This will reset #response to an empty state, and further replies on this
     * object will not do anything.
     */
    void sendReply();

    /**
     * The RPC request received from the client.
     */
    Buffer request;

    /**
     * The reply to the RPC, to send back to the client.
     */
    Buffer response;

  private:
    /**
     * The socket on which to send the reply.
     */
    std::weak_ptr<Server::ServerMessageSocket> messageSocket;

    /**
     * The message ID received with the request. This should be sent back to
     * the client with the response so that the client can match up the
     * response with the right ClientRPC object.
     */
    MessageSocket::MessageId messageId;

    // The Server class uses the private members of this object.
    friend class Server;

    // ServerRPC is non-copyable.
    ServerRPC(const ServerRPC&) = delete;
    ServerRPC& operator=(const ServerRPC&) = delete;

}; // class ServerRPC

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_SERVERRPC_H */
