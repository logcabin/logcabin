/* Copyright (c) 2011-2012 Stanford University
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

/**
 * \file
 * This file declares the interface for DLog's RPC library.
 */

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#ifndef DLOGRPC_H
#define DLOGRPC_H

namespace DLog {
namespace RPC {

/**
 * This exception is thrown when failing to establish a connection to a host.
 */
class HostConnectFailure : public std::exception {
};

/**
 * This exception is thrown when an RPC is attempted while the host is not
 * connected.
 */
class HostNotConnected : public std::exception {
};

/// RPC Service Id
typedef uint16_t ServiceId;
/// RPC Opcode
typedef uint16_t Opcode;
/// RPC Message Id
typedef uint64_t MessageId;

/// RPC Service Ids
enum {
    SERVICE_DLOG,
    SERVICE_REPLICATION,
};

/**
 * Represents a single RPC message and contains helper functions to serialize
 * and deserialize the message header.
 */
class Message {
  public:
    /**
     * Constructor.
     */
    Message();
    ~Message();
    /**
     * Serialize the message header.
     * \param buf
     *      Blob to append to the message.
     */
    void serializeHeader();
    void deserializeHeader();
    void setPayload(void* buf, uint32_t len);
    void *getPayload();
    uint32_t getPayloadLen();
    Opcode rpcOp;
    ServiceId rpcService;
    MessageId rpcId;
  private:
    /// Header size.
    static const uint32_t HEADER_SIZE = 16;
    /// Buffer for message header.
    char header[HEADER_SIZE];
    /// Message payload.
    void *payload;
    /// Payload length.
    uint32_t payloadLen;
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
};

/**
 * RPC service interface that all services derive from.
 */
class Service {
  public:
    virtual ~Service() = 0;
    /**
     * Return the service id of this service.
     * \return
     *      Service id.
     */
    virtual ServiceId getServiceId() const = 0;
    /**
     * Process an incoming service request.
     * \param op
     *      RPC opcode
     * \param request
     *      RPC request message
     * \param reply
     *      A reply message that has a header already initialized.
     * \return
     *      RPC response message.
     */
    virtual void processMessage(Opcode op,
                                std::unique_ptr<Message> request,
                                std::unique_ptr<Message> reply) = 0;
  private:
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
};

/**
 * RPC response object that is used to receive callbacks and wait for the
 * response to a request.
 */
class Response {
  public:
    /**
     * Constructor.
     */
    Response();
    virtual ~Response();
    /**
     * Is the response ready for processing?
     * \return
     *      True if the message response is ready, otherwise false.
     */
    bool isReady();
    /**
     * Get the message id.
     * \return
     *      The message id.
     */
    MessageId getMessageId();
    /**
     * Get the message response.
     */
    virtual Message& getResponse();
    /**
     * An event callback a derived class may implement to process the response.
     * \param msg
     *      The received response.
     */
    virtual void processResponse(Message& msg);
    /**
     * An event callback a derived class may implement to process a timeout or
     * other error.
     */
    virtual void processFailure();
  private:
    /// Is the message ready?
    bool ready;
    /// Message id
    MessageId msgId;
    /// Message response
    Message* response;
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;
};

/**
 * RPC server class handles incoming requests and deliver them to a registered 
 * Service.
 */
class Server {
  public:
    Server(uint16_t port);
    ~Server();
    /**
     * Register an Service object with this session.
     * \param s
     *      A pointer to an Service object.
     */
    void registerService(std::unique_ptr<Service> s);
    /**
     * Send a reply back to the caller. Only service objects should send this
     * type of message.
     * \param reply
     *      Message to reply back with.
     */
    void sendResponse(std::unique_ptr<Message> reply);
  private:
};

class EventLoop;

/**
 * RPC clients may only initiate RPC requests and receive responses.
 */
class Client {
  public:
    /**
     * Constructor.
     * \param loop
     *      EventLoop object to attach to.
     */
    Client(EventLoop& loop);
    ~Client();
    /**
     * Connect to a server.
     * \param host
     *      Server host ip.
     * \param port
     *      Server port number.
     */
    void connect(const std::string& host, uint16_t port);
    void disconnect();
    /**
     * Send a message and register a response object.
     * \param response
     *      Response object for this message.
     * \param message
     *      Message to transmit.
     */
    void send(Response& response, Message& message);
    Response send(Message& message);
  private:
};

} // namespace
} // namespace

#endif /* DLOGRPC_H */
