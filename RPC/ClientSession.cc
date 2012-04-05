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

#include "Core/Debug.h"
#include "RPC/ClientSession.h"

/**
 * The number of milliseconds to wait until the client gets suspicious about
 * the server not responding. After this amount of time elapses, the client
 * will send a ping to the server. If no response is received within another
 * TIMEOUT_MS milliseconds, the session is closed.
 *
 * TODO(ongaro): How should this value be chosen?
 * Ideally, you probably want this to be set to something like the 99-th
 * percentile of your RPC latency.
 *
 * TODO(ongaro): How does this interact with TCP?
 */
enum { TIMEOUT_MS = 100 };

/**
 * A message ID reserved for ping messages used to check the server's liveness.
 * No real RPC will ever be assigned this ID.
 */
enum { PING_MESSAGE_ID = 0 };

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

    if (messageId == PING_MESSAGE_ID) {
        if (session.numActiveRPCs > 0 && session.activePing) {
            // The server has shown that it is alive for now.
            // Let's get suspicious again in another TIMEOUT_MS.
            session.activePing = false;
            session.timer.schedule(TIMEOUT_MS * 1000 * 1000);
        } else {
            LOG(DBG, "Received an unexpected ping response. This can happen "
                     "for a number of reasons and is no cause for alarm. For "
                     "example, this happens if a ping request was sent out, "
                     "then all RPCs completed before the ping response "
                     "arrived.");
        }
        return;
    }

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

    // Book-keeping for timeouts
    --session.numActiveRPCs;
    if (session.numActiveRPCs == 0)
        session.timer.deschedule();
    else
        session.timer.schedule(TIMEOUT_MS * 1000 * 1000);

    // Fill in the response
    response.ready = true;
    response.reply = std::move(message);
    // This is inefficient when there are many RPCs outstanding, but that's
    // not the expected case.
    session.responseReceived.notify_all();
}

void
ClientSession::ClientMessageSocket::onDisconnect()
{
    LOG(DBG, "Disconnected from server %s",
        session.address.toString().c_str());
    std::unique_lock<std::mutex> mutexGuard(session.mutex);
    if (session.errorMessage.empty()) {
        // Fail all current and future RPCs.
        session.errorMessage = ("Disconnected from server " +
                                session.address.toString());
        // Notify any waiting RPCs.
        session.responseReceived.notify_all();
    }
}

////////// ClientSession::Response //////////

ClientSession::Response::Response()
    : ready(false)
    , reply()
{
}

////////// ClientSession::Timer //////////

ClientSession::Timer::Timer(ClientSession& session)
    : Event::Timer(session.eventLoop)
    , session(session)
{
}

void
ClientSession::Timer::handleTimerEvent()
{
    std::unique_lock<std::mutex> mutexGuard(session.mutex);

    // Handle "spurious" wake-ups.
    if (!session.messageSocket ||
        session.numActiveRPCs == 0 ||
        !session.errorMessage.empty()) {
        return;
    }

    // Send a ping or expire the session.
    if (!session.activePing) {
        LOG(DBG, "ClientSession is suspicious. Sending ping.");
        session.activePing = true;
        session.messageSocket->sendMessage(PING_MESSAGE_ID, Buffer());
        schedule(TIMEOUT_MS * 1000 * 1000);
    } else {
        LOG(DBG, "ClientSession to %s timed out.",
            session.address.toString().c_str());
        // Fail all current and future RPCs.
        session.errorMessage = ("Server " +
                                session.address.toString() +
                                " timed out");
        // Notify any waiting RPCs.
        session.responseReceived.notify_all();
    }
}

////////// ClientSession //////////

ClientSession::ClientSession(Event::Loop& eventLoop,
                             const Address& address,
                             uint32_t maxMessageLength)
    : self() // makeSession will fill this in shortly
    , eventLoop(eventLoop)
    , address(address)
    , messageSocket()
    , timer(*this)
    , mutex()
    , nextMessageId(1) // 0 is reserved for PING_MESSAGE_ID
    , responseReceived()
    , responses()
    , errorMessage()
    , numActiveRPCs(0)
    , activePing(false)
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
    timer.deschedule();
    for (auto it = responses.begin(); it != responses.end(); ++it)
        delete it->second;
}

OpaqueClientRPC
ClientSession::sendRequest(Buffer request)
{
    MessageSocket::MessageId messageId;
    {
        std::unique_lock<std::mutex> mutexGuard(mutex);
        messageId = nextMessageId;
        ++nextMessageId;
        responses[messageId] = new Response();

        ++numActiveRPCs;
        if (numActiveRPCs == 1) {
            // activePing's value was undefined while numActiveRPCs = 0
            activePing = false;
            timer.schedule(TIMEOUT_MS * 1000 * 1000);
        }
    }
    // Release the mutex before sending so that receives can be processed
    // simultaneously with sends.
    if (messageSocket)
        messageSocket->sendMessage(messageId, std::move(request));
    OpaqueClientRPC rpc;
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

std::string
ClientSession::toString() const
{
    std::string error = getErrorMessage();
    if (error.empty()) {
        return "Active session to " + address.toString();
    } else {
        // error will already include the server's address.
        return "Closed session: " + error;
    }
}

void
ClientSession::cancel(OpaqueClientRPC& rpc)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    std::unique_lock<std::mutex> mutexGuard(mutex);

    --numActiveRPCs;
    // Even if numActiveRPCs == 0, it's simpler here to just let the timer wake
    // up an extra time and clean up. Otherwise, we'd need to grab an
    // Event::Loop::Lock prior to the mutex to call deschedule() without
    // inducing deadlock.

    auto it = responses.find(rpc.responseToken);
    assert(it != responses.end());
    delete it->second;
    responses.erase(it);
}

void
ClientSession::update(OpaqueClientRPC& rpc)
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
ClientSession::wait(OpaqueClientRPC& rpc)
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
