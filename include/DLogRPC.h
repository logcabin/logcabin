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
#include <unordered_map>
#include <mutex>
#include <condition_variable>

#include "DLogEvent.h"

#ifndef DLOGRPC_H
#define DLOGRPC_H

namespace DLog {
namespace RPC {

// Forward declarations
class Server;
class ServerConnection;
class ServerListener;

/// RPC Service Id
typedef uint16_t ServiceId;
/// RPC Opcode
typedef uint16_t Opcode;
/// RPC Message Id
typedef uint64_t MessageId;
/// RPC Connection Id
typedef uint32_t ConnectionId;

/// RPC Service Ids
enum class RPCServices {
    ECHO,
    DLOG,
    REPLICATION,
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
     */
    void serializeHeader();
    /**
     * Deserialize the message header.
     */
    void deserializeHeader();
    /**
     * Set the payload and payload length. The caller owns the pointer to the
     * buffer and must free the memory themselves.
     */
    void setPayload(char* buf, uint32_t len);
    /**
     * Get the payload.
     */
    char *getPayload() const;
    /**
     * Get the payloads length.
     */
    uint32_t getPayloadLength() const;
    /**
     * RPC Opcode
     */
    Opcode rpcOp;
    /**
     * RPC Service Id
     */
    ServiceId rpcService;
    /**
     * RPC Message Id
     */
    MessageId rpcId;
    /**
     * Connection Id. This field is set by the Server or Client object to
     * identify the connection which the message came from or should be
     * routed to.
     */
    ConnectionId connectionId;
    /// Header size.
    static const uint32_t HEADER_SIZE = 16;
  private:
    /**
     * Buffer for a serialized message header. A serialized header contains
     * a four byte payload length, two byte RPC opcode, two byte RPC service,
     * and a eight byte message id.
     */
    uint8_t header[HEADER_SIZE];
    /// Message payload.
    char *payload;
    /// Payload length.
    uint32_t payloadLen;
    friend class Client;
    friend class ServerConnection;
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
};

/**
 * RPC service interface that all services derive from.
 */
class Service {
  public:
    virtual ~Service() { }
    /**
     * Return the service id of this service.
     * \return
     *      Service id.
     */
    virtual ServiceId getServiceId() const = 0;
    /**
     * Process an incoming service request.
     * \param server
     *      RPC server instance.
     * \param op
     *      RPC opcode
     * \param request
     *      RPC request message
     */
    virtual void processMessage(Server& server,
                                Opcode op,
                                Message& request) = 0;
  protected:
    Service() { }
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
    explicit Response(MessageId id);
    virtual ~Response();
    /**
     * Is the response ready for processing?
     * \return
     *      True if the message response is ready or failure received,
     *      otherwise false the request is still in-flight.
     */
    bool isReady();
    /**
     * Was this a failed response?
     * \return
     *      True if some error or timeout was triggered.
     */
    bool isFailed();
    /**
     * Get the message id.
     * \return
     *      The message id.
     */
    MessageId getMessageId();
    /**
     * Get the message response.
     */
    Message& getResponse();
    /**
     * Wait for the message to be received. This function only works if the
     * default callback methods processResponse and processFailure are called.
     */
    void wait();
  protected:
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
    /**
     * Set the message response object. The ownership of that object is always
     * transfered to the Response object that will free the message payload.
     */
    void setResponse(Message *msg);
    /// Is the message ready or failed?
    bool ready;
    /// Was there a failure or is the message valid?
    bool failed;
    /// Message id
    MessageId msgId;
    /// Message response
    Message *response;
    /// Response state mutex
    std::mutex respMutex;
    /// Response condition variable
    std::condition_variable respCondition;
    friend class Client;
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;
};

/**
 * RPC server class handles incoming requests and deliver them to a registered 
 * Service.
 */
class Server {
  public:
    Server(EventLoop &eventLoop, uint16_t port);
    ~Server();
    /**
     * Register an Service object with this session.
     * \param s
     *      A pointer to an Service object.
     */
    void registerService(Service* s);
    /**
     * Send a reply back to the caller. Only service objects should send this
     * type of message.
     * \param reply
     *      Message to reply back with.
     */
    void sendResponse(Message& reply);
  private:
    /// List of registered services
    std::unordered_map<ServiceId, Service*> services;
    /// List of connected connections
    std::unordered_map<ConnectionId, ServerConnection*> connections;
    /// Server listener
    ServerListener* listener;
    /// Event loop pointer
    EventLoop *loop;
    friend class ServerConnection;
    friend class ServerListener;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
};

class EventLoop;

/**
 * RPC clients may only initiate RPC requests and receive responses.
 */
class Client : private EventSocket {
  public:
    /**
     * Constructor.
     * \param loop
     *      EventLoop object to attach to.
     */
    explicit Client(EventLoop& loop);
    ~Client();
    /**
     * Connect to a server.
     * \param host
     *      Server host ip.
     * \param port
     *      Server port number.
     */
    bool connect(const std::string& host, uint16_t port);
    /**
     * Disconnect from server.
     */
    void disconnect();
    /**
     * Asynchronously send a message.
     * \param message
     *      Message to transmit.
     * \return
     *      A response object that encapsulates a received message.
     */
    Response* send(Message& message);
  protected:
    /// Read callback
    virtual void readCB();
    /// Write callback
    virtual void writeCB();
    /// Event callback
    virtual void eventCB(EventMask events, int errnum);
  private:
    /// Connection state machine states.
    enum class ConnectionState {
        HEADER,
        BODY,
    };
    /// Current connection state.
    ConnectionState connState;
    /// Temporary message
    Message *msg;
    /// Next message id.
    MessageId nextId;
    /// Map of outstanding message requests.
    std::unordered_map<MessageId, Response*> outstanding;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
};

} // namespace
} // namespace

#endif /* DLOGRPC_H */
