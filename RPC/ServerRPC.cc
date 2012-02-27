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

#include "RPC/ServerRPC.h"

namespace LogCabin {
namespace RPC {

ServerRPC::ServerRPC()
    : request()
    , response()
    , messageSocket()
    , messageId(~0UL)
    , responseTarget(NULL)
{
}

ServerRPC::ServerRPC(
        std::weak_ptr<Server::ServerMessageSocket> messageSocket,
        MessageSocket::MessageId messageId,
        Buffer request)
    : request(std::move(request))
    , response()
    , messageSocket(messageSocket)
    , messageId(messageId)
    , responseTarget(NULL)
{
}

ServerRPC::ServerRPC(ServerRPC&& other)
    : request(std::move(other.request))
    , response(std::move(other.response))
    , messageSocket(std::move(other.messageSocket))
    , messageId(std::move(other.messageId))
    , responseTarget(std::move(other.responseTarget))
{
}

ServerRPC::~ServerRPC()
{
}

ServerRPC&
ServerRPC::operator=(ServerRPC&& other)
{
    request = std::move(other.request);
    response = std::move(other.response);
    messageSocket = std::move(other.messageSocket);
    messageId = std::move(other.messageId);
    responseTarget = std::move(other.responseTarget);
    return *this;
}

void
ServerRPC::closeSession()
{
    std::shared_ptr<Server::ServerMessageSocket> socket = messageSocket.lock();
    if (socket)
        socket->close();
    messageSocket.reset();
    responseTarget = NULL;
}

void
ServerRPC::sendReply()
{
    std::shared_ptr<Server::ServerMessageSocket> socket = messageSocket.lock();
    if (socket) {
        socket->sendMessage(messageId, std::move(response));
    } else {
        // During normal operation, this indicates that either the socket has
        // been disconnected or the reply has already been sent.

        // For unit testing only, we can store replies from mock RPCs
        // that have no sessions.
        if (responseTarget != NULL) {
            *responseTarget = std::move(response);
            responseTarget = NULL;
        }

        // Drop the reply on the floor.
        response.reset();
    }
    // Prevent the server from replying again.
    messageSocket.reset();
}

} // namespace LogCabin::RPC
} // namespace LogCabin
