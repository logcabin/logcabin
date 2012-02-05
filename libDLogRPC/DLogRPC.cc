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

#include <assert.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <unordered_map>

#include "Debug.h"
#include "DLogRPC.h"
#include "DLogRPCInt.h"

namespace DLog {
namespace RPC {

/********************************************************************
 *
 *
 * class Message
 *
 *
 ********************************************************************/

Message::Message()
    : rpcOp(0), rpcService(0), rpcId(0),
      connectionId(0), payload(), payloadLen(0)
{
}

Message::~Message()
{
}

void
Message::serializeHeader()
{
    /*
     * Could use ntoh and hton functions but it seems simply enough
     * to serialize everything manually. We do this because there is
     * no cross-platform POSIX defined function for dealing with 64-bit
     * integers.
     */
    // 4 Byte Payload Length
    header[0] = static_cast<char>(payloadLen >> 24);
    header[1] = static_cast<char>((payloadLen >> 16) & 0xff);
    header[2] = static_cast<char>((payloadLen >> 8) & 0xff);
    header[3] = static_cast<char>(payloadLen & 0xff);
    // 2 Byte RPC Opcode
    header[4] = static_cast<char>(rpcOp >> 8);
    header[5] = static_cast<char>(rpcOp & 0xff);
    // 2 Byte RPC Service Id
    header[6] = static_cast<char>(rpcService >> 8);
    header[7] = static_cast<char>(rpcService & 0xff);
    // 8 Byte Message Id
    header[8] = static_cast<char>((rpcId >> 56) & 0xff);
    header[9] = static_cast<char>((rpcId >> 48) & 0xff);
    header[10] = static_cast<char>((rpcId >> 40) & 0xff);
    header[11] = static_cast<char>((rpcId >> 32) & 0xff);
    header[12] = static_cast<char>((rpcId >> 24) & 0xff);
    header[13] = static_cast<char>((rpcId >> 16) & 0xff);
    header[14] = static_cast<char>((rpcId >> 8) & 0xff);
    header[15] = static_cast<char>(rpcId & 0xff);
}

void
Message::deserializeHeader()
{
    payloadLen = (uint32_t)(header[0] << 24 |
                            header[1] << 16 |
                            header[2] << 8 |
                            header[3]);
    rpcOp = (Opcode)(header[4] << 8 | header[5]);
    rpcService = (ServiceId)(header[6] << 8 | header[7]);
    rpcId = (MessageId)((uint64_t)header[8] << 56 |
                        (uint64_t)header[9] << 48 |
                        (uint64_t)header[10] << 40 |
                        (uint64_t)header[11] << 32 |
                        (uint64_t)header[12] << 24 |
                        (uint64_t)header[13] << 16 |
                        (uint64_t)header[14] << 8 |
                        (uint64_t)header[15]);
}

void
Message::setPayload(char* buf, uint32_t len)
{
    payload = buf;
    payloadLen = len;
}

char *Message::getPayload() const
{
    return payload;
}

uint32_t
Message::getPayloadLength() const
{
    return payloadLen;
}

/********************************************************************
 *
 *
 * class Response
 *
 *
 ********************************************************************/

Response::Response(MessageId id)
    : ready(false),
      failed(false),
      msgId(id),
      response(),
      respMutex(),
      respCondition()
{
}

Response::~Response()
{
    if (response) {
        delete[] response->getPayload();
        delete response;
    }
}

bool
Response::isReady()
{
    return ready;
}

bool
Response::isFailed()
{
    return failed;
}

MessageId
Response::getMessageId()
{
    return msgId;
}

void
Response::setResponse(Message *msg)
{
    response = msg;
}

Message&
Response::getResponse()
{
    return *response;
}

void
Response::wait()
{
    std::unique_lock<std::mutex> lk(respMutex);
    while (!ready) {
        respCondition.wait(lk);
    }
}

void
Response::processResponse(Message& msg)
{
    std::lock_guard<std::mutex> lk(respMutex);
    failed = false;
    ready = true;
    respCondition.notify_all();
}

void
Response::processFailure()
{
    std::lock_guard<std::mutex> lk(respMutex);
    failed = true;
    ready = true;
    respCondition.notify_all();
}

/********************************************************************
 *
 *
 * class Server
 *
 *
 ********************************************************************/

Server::Server(EventLoop &eventLoop, uint16_t port)
    : services(),
      connections(),
      listener(),
      loop(&eventLoop)
{
    listener = new ServerListener(*this, eventLoop);
    listener->bind(port);
}

Server::~Server()
{
    delete listener;
    for (auto it = connections.begin(); it != connections.end(); it++)
    {
        delete it->second;
        connections.erase(it);
    }
}

void
Server::registerService(Service* s)
{
    services[s->getServiceId()] = s;
}

void
Server::sendResponse(Message& reply)
{
    ServerConnection *connection = connections[reply.connectionId];

    connection->sendResponse(reply);
}

/********************************************************************
 *
 *
 * class ServerConnection
 *
 *
 ********************************************************************/

ServerConnection::ServerConnection(Server &rpcServer,
                                   EventLoop &eventLoop,
                                   ConnectionId id,
                                   int fd)
    : EventSocket(eventLoop),
      connectionId(id),
      msg(),
      connState(ConnectionState::HEADER),
      server(&rpcServer)
{
    bind(fd);
    setReadWatermark(Message::HEADER_SIZE);
}

ServerConnection::~ServerConnection()
{
}

void
ServerConnection::readCB()
{
    if (connState == ConnectionState::HEADER) {
        read(msg.header, Message::HEADER_SIZE);
        msg.deserializeHeader();
        msg.connectionId = connectionId;
        connState = ConnectionState::BODY;

        if (getLength() < msg.getPayloadLength()) {
            setReadWatermark(msg.getPayloadLength());
            return;
        }
        // Fall through if we have received enough data.
    }

    if (connState == ConnectionState::BODY) {
        uint32_t length = msg.getPayloadLength();
        char *body = new char[length];
        auto service = server->services.find(msg.rpcService);

        if (service != server->services.end()) {
            read(body, length);
            msg.setPayload(body, length);
            service->second->processMessage(*server, msg.rpcOp, msg);
        } else {
            discard(length);
            LOG(WARNING, "Service %d is not registered", msg.rpcService);
        }

        connState = ConnectionState::HEADER;
        delete[] body;
    }

    return;
}

void
ServerConnection::writeCB()
{
    // Nothing to do for now.
}

void
ServerConnection::eventCB(EventMask events, int errnum)
{
    /*
     * For now any error or socket close will be treated the same way
     * and we will close the socket.
     */
    if ((events & EventSocket::Error) || (events & EventSocket::Eof)) {
        LOG(DBG, "Connection %d closed.\n", connectionId);
        server->connections.erase(connectionId);
        delete this;
        return;
    }

    // Assert that connected is the only other event we receive.
    assert(events == EventSocket::Connected);
    return;
}

void
ServerConnection::sendResponse(Message& message)
{
    lock();

    assert(message.connectionId == connectionId);
    message.serializeHeader();
    write(message.header, Message::HEADER_SIZE);
    write(message.getPayload(), message.getPayloadLength());

    unlock();
}

/********************************************************************
 *
 *
 * class ServerListener
 *
 *
 ********************************************************************/

ServerListener::ServerListener(Server &rpcServer, EventLoop &eventLoop)
    : EventListener(eventLoop),
      nextId(0),
      server(&rpcServer),
      loop(&eventLoop)
{
    /*
     * Choose a random starting value for connection id. This should
     * not be necessary, but it just makes debugging a bit easier.
     */
    nextId = rand(); // NOLINT
}

ServerListener::~ServerListener()
{
}

void
ServerListener::acceptCB(int fd)
{
    ServerConnection *connection;

    // Construct and register the ServerConnection object.
    connection = new ServerConnection(*server, *loop, nextId, fd);
    server->connections[nextId] = connection;
    LOG(DBG, "Connection accepted: %d", nextId);

    // Increment nextId
    nextId++;
}

void
ServerListener::errorCB(void)
{
    assert(false);
}

/********************************************************************
 *
 *
 * class Client
 *
 *
 ********************************************************************/

Client::Client(EventLoop& loop)
    : EventSocket(loop),
      connState(ConnectionState::HEADER),
      msg(),
      nextId(0),
      outstanding()
{
    nextId = ((uint64_t)rand() << 32) + rand(); // NOLINT
}

Client::~Client()
{
    for (auto it = outstanding.begin(); it != outstanding.end(); it++)
    {
        it->second->processFailure();
        outstanding.erase(it);
    }
}

bool
Client::connect(const std::string& host, uint16_t port)
{
    connState = ConnectionState::HEADER;
    return EventSocket::connect(host.c_str(), port);
}

void
Client::disconnect()
{
    EventSocket::disconnect();
}

Response*
Client::send(Message& message)
{
    Response *resp;

    lock();

    message.rpcId = nextId;
    resp = new Response(nextId);

    message.serializeHeader();
    write(message.header, Message::HEADER_SIZE);
    write(message.getPayload(), message.getPayloadLength());

    outstanding[nextId] = resp;
    nextId++;

    unlock();
    return resp;
}

void
Client::readCB()
{
    if (connState == ConnectionState::HEADER) {
        msg = new Message();
        read(msg->header, Message::HEADER_SIZE);
        msg->deserializeHeader();
        msg->connectionId = 0;
        connState = ConnectionState::BODY;

        if (getLength() < msg->getPayloadLength()) {
            setReadWatermark(msg->getPayloadLength());
            return;
        }
        // Fall through if we have received enough data.
    }

    if (connState == ConnectionState::BODY) {
        assert(msg != NULL);
        uint32_t length = msg->getPayloadLength();
        char *body = new char[length];
        auto response = outstanding.find(msg->rpcId);

        if (response != outstanding.end()) {
            read(body, length);
            response->second->setResponse(msg);
            response->second->processResponse(*msg);
            msg = NULL;
        } else {
            LOG(WARNING, "Response object %lx not found", msg->rpcId);
            discard(length);
            delete[] body;
            delete msg;
        }
        connState = ConnectionState::HEADER;
    }

    return;
}

void
Client::writeCB()
{
    // Nothing to do for now.
}
void
Client::eventCB(EventMask events, int errnum)
{
    /*
     * For now any error or socket close will be treated the same way
     * and we will close the socket.
     */
    if ((events & EventSocket::Error) || (events & EventSocket::Eof)) {
        LOG(DBG, "Client disconnected.");
        for (auto it = outstanding.begin(); it != outstanding.end(); it++)
        {
            it->second->processFailure();
            outstanding.erase(it);
        }
        disconnect();
        return;
    }

    // Assert that connected is the only other event we receive.
    assert(events == EventSocket::Connected);
    return;
}

} // namespace
} // namespace

