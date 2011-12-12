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

/**
 * Represents a single contiguous segment of data to transmit or receive.
 */
class Segment {
  public:
    /**
     * Constructor.
     * \param data
     *      Creates an RPC.
     * \param len
     *      Initial length of the segment.
     */
    Segment(const void* data, uint32_t len)
    {
        buf = data;
        length = len;
    }
    ~Segment() { }
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
class Message {
  public:
    /**
     * Constructor.
     */
    Message();
    ~Message();
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
    std::vector<Segment> segments;
    friend class Transport;
};

/// RPC Service Id
typedef uint16_t ServiceId;
/// RPC Opcode
typedef uint16_t Opcode;
/// RPC Message Id
typedef uint64_t MessageId;

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
     * \param msg
     *      RPC request message
     * \return
     *      RPC response message.
     */
    virtual Message& processMessage(Opcode op, Message& msg) = 0;
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

class Transport {
};

/**
 * Manages an RPC session.
 */
class Session {
  public:
    /**
     * Constructor
     * \param t
     *      Transport to use for this RPC session.
     * \param hostname
     *      Hostname of the peer.
     */
    Session(Transport& t, const std::string& hostname);
    /**
     * Register an Service object with this session.
     * \param s
     *      A pointer to an Service object.
     */
    void registerService(std::unique_ptr<Service> s);
    /**
     * Send a message and register a response object.
     * \param response
     *      Response object for this message.
     * \param message
     *      Message to transmit.
     */
    void send(Response& response, Message& message);
  private:
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

} // namespace
} // namespace

#endif /* DLOGRPC_H */
