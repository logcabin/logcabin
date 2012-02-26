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

/**
 * \file
 * This file documents low-level headers used in the protocol between LogCabin
 * clients and servers.
 */

#include <cinttypes>

#ifndef LOGCABIN_PROTO_CLIENT_H
#define LOGCABIN_PROTO_CLIENT_H

namespace LogCabin {
namespace Protocol {
namespace Client {

/**
 * The maximum number of bytes per RPC request or response, including these
 * headers. This is set to slightly over 1 MB because the maximum size of log
 * entries is 1 MB.
 */
const uint32_t MAX_MESSAGE_LENGTH = 1024 + 1024 * 1024;

/**
 * In version 1 of the protocol, this is the header format for requests from
 * clients to servers.
 */
struct RequestHeaderVersion1 {
    /**
     * Convert the contents to host order from big endian (how this header
     * should be transferred on the network).
     */
    void fromBigEndian();
    /**
     * Convert the contents to big endian (how this header should be
     * transferred on the network) from host order.
     */
    void toBigEndian();

    /**
     * This is the version of the protocol. It should always be set to 1 for
     * now.
     */
    uint8_t version;

    /**
     * This identifies which RPC is being executed.
     */
    uint8_t opCode;

    // A protocol buffer follows with the request.

} __attribute__((packed));

/**
 * The status codes returned in server responses.
 */
enum class Status : uint8_t {
    /**
     * The server processed the request and returned a valid protocol buffer
     * with the results.
     */
    OK              = 0,
    /**
     * The server did not like the version number provided in the request
     * header. If the client gets this, it should fall back to an older version
     * number or crash.
     */
    INVALID_VERSION = 1,
    /**
     * The server did not like the RPC request. Either it specified an opCode
     * the server didn't understand or a request protocol buffer the server
     * couldn't accept. The client should avoid ever getting this by
     * negotiating with the server about which version of the RPC protocol to
     * use.
     */
    INVALID_REQUEST = 2,
    /**
     * The server is not the current cluster leader. The client should look
     * elsewhere for the cluster leader. The server MAY provide a hint as to
     * who the leader is, in the format of a null-terminated string directly
     * following the response header.
     */
    NOT_LEADER      = 3,
};

/**
 * In version 1 of the protocol, this is the header format for responses from
 * servers to clients.
 */
struct ResponseHeaderVersion1 {
    /**
     * Convert the contents to host order from big endian (how this header
     * should be transferred on the network).
     */
    void fromBigEndian();
    /**
     * Convert the contents to big endian (how this header should be
     * transferred on the network) from host order.
     */
    void toBigEndian();

    /**
     * The error code returned by the server.
     */
    Status status;

    // If status == OK, a protocol buffer follows with the response.
    // If status == NOT_LEADER, a null-terminated character string may follow
    //                          describing where to find the leader.

} __attribute__((packed));

} // namespace LogCabin::Protocol::Client
} // namespace LogCabin::Protocol
} // namespace LogCabin

#endif // LOGCABIN_PROTO_CLIENT_H
