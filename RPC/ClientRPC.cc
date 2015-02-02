/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014 Diego Ongaro
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

#include "Core/Debug.h"
#include "RPC/Protocol.h"
#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"
#include "RPC/ProtoBuf.h"

namespace LogCabin {
namespace RPC {

using RPC::Protocol::RequestHeaderPrefix;
using RPC::Protocol::RequestHeaderVersion1;
using RPC::Protocol::ResponseHeaderPrefix;
using RPC::Protocol::ResponseHeaderVersion1;
typedef RPC::Protocol::Status ProtocolStatus;

ClientRPC::ClientRPC(std::shared_ptr<RPC::ClientSession> session,
                     uint16_t service,
                     uint8_t serviceSpecificErrorVersion,
                     uint16_t opCode,
                     const google::protobuf::Message& request)
    : opaqueRPC() // placeholder, set again below
{
    // Serialize the request into a Buffer
    Core::Buffer requestBuffer;
    ProtoBuf::serialize(request, requestBuffer,
                        sizeof(RequestHeaderVersion1));
    auto& requestHeader =
        *static_cast<RequestHeaderVersion1*>(requestBuffer.getData());
    requestHeader.prefix.version = 1;
    requestHeader.prefix.toBigEndian();
    requestHeader.service = service;
    requestHeader.serviceSpecificErrorVersion = serviceSpecificErrorVersion;
    requestHeader.opCode = opCode;
    requestHeader.toBigEndian();

    // Send the request to the server
    assert(session); // makes debugging more obvious for somewhat common error
    opaqueRPC = session->sendRequest(std::move(requestBuffer));
}

ClientRPC::ClientRPC()
    : opaqueRPC()
{
}

ClientRPC::ClientRPC(ClientRPC&& other)
    : opaqueRPC(std::move(other.opaqueRPC))
{
}

ClientRPC::~ClientRPC()
{
}

ClientRPC&
ClientRPC::operator=(ClientRPC&& other)
{
    opaqueRPC = std::move(other.opaqueRPC);
    return *this;
}

void
ClientRPC::cancel()
{
    opaqueRPC.cancel();
}

bool
ClientRPC::isReady()
{
    return opaqueRPC.getStatus() != OpaqueClientRPC::Status::NOT_READY;
}

ClientRPC::Status
ClientRPC::waitForReply(google::protobuf::Message* response,
                        google::protobuf::Message* serviceSpecificError,
                        TimePoint timeout)
{
    opaqueRPC.waitForReply(timeout);
    switch (opaqueRPC.getStatus()) {
        case OpaqueClientRPC::Status::NOT_READY:
            if (Clock::now() > timeout) {
                return Status::TIMEOUT;
            } else {
                PANIC("Waited for RPC but not ready and "
                      "timeout hasn't elapsed (timeout=%s, now=%s)",
                      Core::StringUtil::toString(timeout).c_str(),
                      Core::StringUtil::toString(Clock::now()).c_str());
            }
        case OpaqueClientRPC::Status::OK:
            break;
        case OpaqueClientRPC::Status::ERROR:
            return Status::RPC_FAILED;
        case OpaqueClientRPC::Status::CANCELED:
            return Status::RPC_CANCELED;
    }
    const Core::Buffer& responseBuffer = *opaqueRPC.peekReply();

    // Extract the response's status field.
    if (responseBuffer.getLength() < sizeof(ResponseHeaderPrefix)) {
        PANIC("The response from the server was too short to be valid "
              "(%u bytes). This probably indicates network or memory "
              "corruption.", responseBuffer.getLength());
    }
    ResponseHeaderPrefix responseHeaderPrefix =
        *static_cast<const ResponseHeaderPrefix*>(responseBuffer.getData());
    responseHeaderPrefix.fromBigEndian();
    if (responseHeaderPrefix.status == ProtocolStatus::INVALID_VERSION) {
        // The server doesn't understand this version of the header
        // protocol. Since this library only runs version 1 of the
        // protocol, this shouldn't happen if servers continue supporting
        // version 1.
        PANIC("This client is too old to talk to the server. "
              "You'll need to update your client library.");
    }

    if (responseBuffer.getLength() < sizeof(ResponseHeaderVersion1)) {
        PANIC("The response from the server was too short to be valid. "
              "This probably indicates network or memory corruption.");
    }
    ResponseHeaderVersion1 responseHeader =
        *static_cast<const ResponseHeaderVersion1*>(responseBuffer.getData());
    responseHeader.fromBigEndian();

    switch (responseHeader.prefix.status) {

        // The RPC succeeded. Parse the response into a protocol buffer.
        case ProtocolStatus::OK:
            if (response != NULL &&
                !RPC::ProtoBuf::parse(responseBuffer, *response,
                                      sizeof(responseHeader))) {
                PANIC("Could not parse the protocol buffer out of the server "
                      "response");
            }
            return Status::OK;

        // The RPC failed in a service-specific way. Parse the response into a
        // protocol buffer.
        case ProtocolStatus::SERVICE_SPECIFIC_ERROR:
            if (serviceSpecificError != NULL &&
                !RPC::ProtoBuf::parse(responseBuffer, *serviceSpecificError,
                                      sizeof(responseHeader))) {
                PANIC("Could not parse the protocol buffer out of the "
                      "service-specific error details");
            }
            return Status::SERVICE_SPECIFIC_ERROR;

        // The server does not have the requested service.
        case ProtocolStatus::INVALID_SERVICE:
            PANIC("The server is not running the requested service.");

        // The server disliked our request. This shouldn't happen because
        // the higher layers of software were supposed to negotiate an RPC
        // protocol version.
        case ProtocolStatus::INVALID_REQUEST:
            PANIC("The server found the request to be invalid. This "
                  "indicates a bug in the client or server in negotiating "
                  "which RPCs the client may legally send to the server.");

        default:
            // The server shouldn't reply back with status codes we don't
            // understand. That's why we gave it a version number in the
            // request header.
            PANIC("Unknown status %u returned from server after sending it "
                  "protocol version 1 in the request header. This probably "
                  "indicates a bug in the server.",
                  uint32_t(responseHeader.prefix.status));
    }

}

std::string
ClientRPC::getErrorMessage() const
{
    return opaqueRPC.getErrorMessage();
}

::std::ostream&
operator<<(::std::ostream& os, ClientRPC::Status status)
{
    typedef ClientRPC::Status Status;
    switch (status) {
        case Status::OK:
            return os << "OK";
        case Status::SERVICE_SPECIFIC_ERROR:
            return os << "SERVICE_SPECIFIC_ERROR";
        case Status::RPC_FAILED:
            return os << "RPC_FAILED";
        case Status::RPC_CANCELED:
            return os << "RPC_CANCELED";
        case Status::TIMEOUT:
            return os << "TIMEOUT";
        default:
            return os << "(INVALID VALUE)";
    }
}

} // namespace LogCabin::RPC
} // namespace LogCabin
