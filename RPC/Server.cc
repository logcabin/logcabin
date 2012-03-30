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

#include <errno.h>
#include <string.h>

#include "Core/Debug.h"
#include "RPC/OpaqueServerRPC.h"
#include "RPC/Server.h"
#include "RPC/Service.h"

/**
 * A message ID reserved for ping messages used to check the server's liveness.
 * No real RPC will ever be assigned this ID.
 */
enum { PING_MESSAGE_ID = 0 };

namespace LogCabin {
namespace RPC {

////////// Server::ServerTCPListener //////////

Server::ServerTCPListener::ServerTCPListener(Server* server)
    : TCPListener(server->eventLoop)
    , server(server)
{
}

void
Server::ServerTCPListener::handleNewConnection(int fd)
{
    if (server == NULL) {
        if (close(fd) != 0)
            LOG(WARNING, "close(%d) failed: %s", fd, strerror(errno));
    } else {
        std::shared_ptr<ServerMessageSocket> messageSocket(
                new ServerMessageSocket(server, fd, server->sockets.size()));
        messageSocket->self = messageSocket;
        server->sockets.push_back(messageSocket);
    }
}

////////// Server::ServerMessageSocket //////////

Server::ServerMessageSocket::ServerMessageSocket(Server* server, int fd,
                                                 size_t socketsIndex)
    : MessageSocket(server->eventLoop, fd, server->maxMessageLength)
    , server(server)
    , socketsIndex(socketsIndex)
    , self()
{
}

void
Server::ServerMessageSocket::onReceiveMessage(MessageId messageId,
                                              Buffer message)
{
    // Reply to ping requests here.
    if (messageId == PING_MESSAGE_ID) {
        LOG(DBG, "Responding to ping");
        sendMessage(PING_MESSAGE_ID, Buffer());
        return;
    }
    if (server != NULL) {
        LOG(DBG, "Handling RPC");
        OpaqueServerRPC rpc(self, messageId, std::move(message));
        server->handleRPC(std::move(rpc));
    }
}

void
Server::ServerMessageSocket::onDisconnect()
{
    LOG(DBG, "Disconnected from client");
    close();
}

void
Server::ServerMessageSocket::close()
{
    if (server != NULL) {
        Event::Loop::Lock lock(server->eventLoop);
        auto& sockets = server->sockets;
        std::swap(sockets[socketsIndex], sockets[sockets.size() - 1]);
        sockets[socketsIndex]->socketsIndex = socketsIndex;
        sockets.pop_back();
    }
}

////////// Server //////////

Server::Server(Event::Loop& eventLoop, uint32_t maxMessageLength)
    : eventLoop(eventLoop)
    , maxMessageLength(maxMessageLength)
    , sockets()
    , listener(this)
{
}

Server::~Server()
{
    // Block the event loop.
    Event::Loop::Lock lockGuard(eventLoop);

    // Stop the listener from accepting new connections
    listener.server = NULL;

    // Stop the socket objects from handling new RPCs and
    // accessing the sockets vector.
    for (auto it = sockets.begin(); it != sockets.end(); ++it) {
        ServerMessageSocket& socket = **it;
        socket.server = NULL;
    }
}

std::string
Server::bind(const Address& listenAddress)
{
    return listener.bind(listenAddress);
}

} // namespace LogCabin::RPC
} // namespace LogCabin
