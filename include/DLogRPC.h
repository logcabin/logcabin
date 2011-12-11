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

/**
 * \file
 * This file declares the interface for DLog's RPC library.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#ifndef DLOGRPC_H
#define DLOGRPC_H

namespace DLogRPC {

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

/**
 * Represents a single contiguous segment of data to transmit or receive.
 */
class RPCSegment {
  public:
    /**
     * Constructor.
     * \param data
     *      Creates an RPC.
     */
    RPCSegment(const void* data, uint32_t len)
    {
        buf = data;
        length = len;
    }
    ~RPCSegment() { }
    /**
     * Grow the segment by extending the length.
     * \param len
     *      Additional length to append to the segment.
     */
    void grow(uint32_t len)
    {
        length += len;
    }
    /// Pointer to buffer.
    const void* buf;
    /// Length of buffer.
    uint32_t length;
};

/**
 * Represents a single RPC message and contains helper functions to serialize
 * and deserialize data.
 */
class RPCMessage {
  public:
    /**
     * Constructor.
     */
    RPCMessage();
    ~RPCMessage();
    /**
     * Serialize a uint8_t to the message.
     * \param i
     *      Integer to serialize into the message.
     */
    void append(uint8_t i);
    /**
     * Serialize a uint16_t to the message.
     * \param i
     *      Integer to serialize into the message.
     */
    void append(uint16_t i);
    /**
     * Serialize a uint32_t to the message.
     * \param i
     *      Integer to serialize into the message.
     */
    void append(uint32_t i);
    /**
     * Serialize a uint64_t to the message.
     * \param i
     *      Integer to serialize into the message.
     */
    void append(uint64_t i);
    /**
     * Serialize a string to the message.
     * \param s
     *      String to serialize into the message.
     */
    void append(const std::string& s);
    /**
     * Serialize a blob to the message.
     * \param buf
     *      Blob to append to the message.
     * \param len
     *      Length of the blob.
     */
    void append(void* buf, uint32_t len);
  private:
    /// Maximum buffer size (not including large blobs).
    static const uint32_t MAXBUFFER_SIZE = 1024;
    /// Buffer for message data (not including large blobs).
    char buffer[MAXBUFFER_SIZE];
    /// List of segments that make up the RPC.
    std::vector<RPCSegment> segments;
    friend class RPCTransport;
};

/// RPC Service Id
typedef uint16_t RPCServiceId;
/// RPC Opcode
typedef uint16_t RPCOpcode;
/// RPC Message Id
typedef uint64_t RPCMessageId;

/**
 * RPC service interface that all services derive from.
 */
class RPCService {
  public:
    virtual ~RPCService() = 0;
    /**
     * Return the service id of this service.
     * \return
     *      Service id.
     */
    virtual RPCServiceId getServiceId() const = 0;
    /**
     * Process an incoming service request.
     * \param op
     *      RPC opcode
     * \param msg
     *      RPC request message
     * \return
     *      RPC response message.
     */
    virtual RPCMessage& processMessage(RPCOpcode op, RPCMessage& msg) = 0;
  private:
    RPCService(const RPCService&) = delete;
    RPCService& operator=(const RPCService&) = delete;
};

/**
 * RPC response object that is used to receive callbacks and wait for the
 * response to a request.
 */
class RPCResponse {
  public:
    /**
     * Constructor.
     */
    RPCResponse();
    virtual ~RPCResponse();
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
    RPCMessageId getMessageId();
    /**
     * Get the message response.
     */
    virtual RPCMessage& getResponse();
    /**
     * An event callback a derived class may implement to process the response.
     * \param msg
     *      The received response.
     */
    virtual void processResponse(RPCMessage& msg);
    /**
     * An event callback a derived class may implement to process a timeout or
     * other error.
     */
    virtual void processFailure();
  private:
    /// Is the message ready?
    bool ready;
    /// Message id
    RPCMessageId msgId;
    /// Message response
    RPCMessage* response;
    RPCResponse(const RPCResponse&) = delete;
    RPCResponse& operator=(const RPCResponse&) = delete;
};

class RPCTransport {
};

/**
 * Manages an RPC session.
 */
class RPCSession {
  public:
    /**
     * Constructor
     * \param t
     *      Transport to use for this RPC session.
     * \param hostname
     *      Hostname of the peer.
     */
    RPCSession(RPCTransport& t, const std::string& hostname);
    /**
     * Register an RPCService object with this session.
     * \param s
     *      A pointer to an RPCService object.
     */
    void registerService(std::unique_ptr<RPCService> s);
    /**
     * Send a message and register a response object.
     * \param response
     *      Response object for this message.
     * \param message
     *      Message to transmit.
     */
    void send(RPCResponse& response, RPCMessage& message);
  private:
    RPCSession(const RPCSession&) = delete;
    RPCSession& operator=(const RPCSession&) = delete;
};

} // namespace

#endif /* DLOGRPC_H */
