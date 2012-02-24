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

#include <assert.h>

#include "include/Debug.h"
#include "RPC/ClientSession.h"

namespace LogCabin {
namespace RPC {

////////// ClientSession::ClientMessageSocket //////////

ClientSession::ClientMessageSocket::ClientMessageSocket(
        ClientSession& session,
        int fd,
        uint32_t maxMessageLength)
    : MessageSocket(session.eventLoop, fd, maxMessageLength)
    , session(session)
{
}

void
ClientSession::ClientMessageSocket::onReceiveMessage(MessageId messageId,
                                                     Buffer message)
{
    std::unique_lock<std::mutex> mutexGuard(session.mutex);
    auto it = session.responses.find(messageId);
    if (it == session.responses.end()) {
        LOG(DBG, "Received an unexpected response with message ID %lu. "
                 "This can happen for a number of reasons and is no cause "
                 "for alarm. For example, this happens if the RPC was "
                 "cancelled before its response arrived.",
                 messageId);
        return;
    }
    Response& response = *it->second;
    if (response.ready) {
        LOG(WARNING, "Received a second response from the server for "
            "message ID %lu. This indicates that either the client or "
            "server is assigning message IDs incorrectly, or "
            "the server is misbehaving. Dropped this response.",
            messageId);
        return;
    }
    response.ready = true;
    response.reply = std::move(message);
    // This is inefficient when there are many RPCs outstanding, but that's
    // not the expected case.
    session.responseReceived.notify_all();
}

void
ClientSession::ClientMessageSocket::onDisconnect()
{
    std::unique_lock<std::mutex> mutexGuard(session.mutex);
    // Fail all current and future RPCs.
    session.errorMessage = "Disconnected from server";
    // Notify any waiting RPCs.
    session.responseReceived.notify_all();
}

////////// ClientSession::Response //////////

ClientSession::Response::Response()
    : ready(false)
    , reply()
{
}

////////// ClientSession //////////

ClientSession::ClientSession(Event::Loop& eventLoop,
                             const Address& address,
                             uint32_t maxMessageLength)
    : self() // makeSession will fill this in shortly
    , eventLoop(eventLoop)
    , messageSocket()
    , mutex()
    , nextMessageId(0)
    , responseReceived()
    , responses()
    , errorMessage()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        errorMessage = "Failed to create socket";
        return;
    }
    int r = connect(fd,
                    address.getSockAddr(),
                    address.getSockAddrLen());
    if (r != 0) {
        errorMessage = "Failed to connect socket to " + address.toString();
        close(fd);
        return;
    }
    messageSocket.reset(new ClientMessageSocket(*this, fd, maxMessageLength));
}

std::shared_ptr<ClientSession>
ClientSession::makeSession(Event::Loop& eventLoop,
                           const Address& address,
                           uint32_t maxMessageLength)
{
    std::shared_ptr<ClientSession> session(
        new ClientSession(eventLoop, address, maxMessageLength));
    session->self = session;
    return session;
}


ClientSession::~ClientSession()
{
    for (auto it = responses.begin(); it != responses.end(); ++it)
        delete it->second;
}

ClientRPC
ClientSession::sendRequest(Buffer request)
{
    MessageSocket::MessageId messageId;
    {
        std::unique_lock<std::mutex> mutexGuard(mutex);
        messageId = nextMessageId;
        ++nextMessageId;
        responses[messageId] = new Response();
    }
    // Release the mutex before sending so that receives can be processed
    // simultaneously with sends.
    if (messageSocket)
        messageSocket->sendMessage(messageId, std::move(request));
    ClientRPC rpc;
    rpc.session = self.lock();
    rpc.responseToken = messageId;
    return rpc;
}

std::string
ClientSession::getErrorMessage() const
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    return errorMessage;
}

void
ClientSession::cancel(ClientRPC& rpc)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    rpc.ready = true;
    rpc.session.reset();
    rpc.reply.reset();
    rpc.errorMessage = "RPC canceled by user";

    std::unique_lock<std::mutex> mutexGuard(mutex);
    auto it = responses.find(rpc.responseToken);
    assert(it != responses.end());
    delete it->second;
    responses.erase(it);
}

void
ClientSession::update(ClientRPC& rpc)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    std::unique_lock<std::mutex> mutexGuard(mutex);
    auto it = responses.find(rpc.responseToken);
    assert(it != responses.end());
    Response* response = it->second;
    if (response->ready)
        rpc.reply = std::move(response->reply);
    else if (!errorMessage.empty())
        rpc.errorMessage = errorMessage;
    else
        return;
    rpc.ready = true;
    rpc.session.reset();

    delete response;
    responses.erase(it);
}

void
ClientSession::wait(ClientRPC& rpc)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    std::unique_lock<std::mutex> mutexGuard(mutex);
    Response* response = responses[rpc.responseToken];
    while (!response->ready && errorMessage.empty())
        responseReceived.wait(mutexGuard);
    if (response->ready)
        rpc.reply = std::move(response->reply);
    else
        rpc.errorMessage = errorMessage;
    rpc.ready = true;
    rpc.session.reset();

    delete response;
    // It's unsafe to keep an iterator from before the wait() call, so it's
    // necessary to look up rpc.responseToken in the map again.
    responses.erase(rpc.responseToken);
}

} // namespace LogCabin::RPC
} // namespace LogCabin
