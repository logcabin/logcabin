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

#include <string.h>
#include <utility>

#include "../proto/dlog.pb.h"
#include "proto/Client.h"
#include "RPC/Buffer.h"
#include "RPC/ProtoBuf.h"
#include "RPC/ServerRPC.h"
#include "Server/ClientService.h"
#include "Server/Globals.h"

namespace LogCabin {
namespace Server {

namespace {

namespace ProtoBuf = DLog::ProtoBuf;
using ProtoBuf::ClientRPC::OpCode;
using Protocol::Client::RequestHeaderPrefix;
using Protocol::Client::RequestHeaderVersion1;
using Protocol::Client::ResponseHeaderPrefix;
using Protocol::Client::ResponseHeaderVersion1;
using Protocol::Client::Status;
using RPC::Buffer;
using RPC::ServerRPC;

/**
 * Reply to the RPC with a status of OK.
 * \param rpc
 *      RPC to reply to.
 * \param payload
 *      A protocol buffer to serialize into the response.
 */
void
reply(ServerRPC rpc, const google::protobuf::Message& payload)
{
    RPC::Buffer buffer;
    RPC::ProtoBuf::serialize(payload, buffer,
                             sizeof(ResponseHeaderVersion1));
    auto& responseHeader =
        *static_cast<ResponseHeaderVersion1*>(buffer.getData());
    responseHeader.prefix.status = Status::OK;
    responseHeader.prefix.toBigEndian();
    responseHeader.toBigEndian();
    rpc.response = std::move(buffer);
    rpc.sendReply();
}

/**
 * Reply to the RPC with a non-OK status.
 * \param rpc
 *      RPC to reply to.
 * \param status
 *      Status code to return.
 * \param extra
 *      Raw bytes to place after the status code. This is intended for
 *      use in Status::NOT_LEADER, which can return a string telling the client
 *      where the leader is.
 */
void
fail(ServerRPC rpc,
     Status status,
     const RPC::Buffer& extra = RPC::Buffer())
{
    uint32_t length = uint32_t(sizeof(ResponseHeaderVersion1) +
                               extra.getLength());
    RPC::Buffer buffer(new char[length],
                       length,
                       RPC::Buffer::deleteArrayFn<char>);
    auto& responseHeader =
        *static_cast<ResponseHeaderVersion1*>(buffer.getData());
    responseHeader.prefix.status = status;
    responseHeader.prefix.toBigEndian();
    responseHeader.toBigEndian();
    memcpy(&responseHeader + 1, extra.getData(), extra.getLength());
    rpc.response = std::move(buffer);
    rpc.sendReply();
}

} // anonymous namespace

ClientService::ClientService(Globals& globals)
    : globals(globals)
{
}

ClientService::~ClientService()
{
}

void
ClientService::handleRPC(ServerRPC rpc)
{
    // Carefully read the headers.
    if (rpc.request.getLength() < sizeof(RequestHeaderPrefix)) {
        fail(std::move(rpc), Status::INVALID_VERSION);
        return;
    }
    auto& requestHeaderPrefix = *static_cast<RequestHeaderPrefix*>(
                                        rpc.request.getData());
    requestHeaderPrefix.fromBigEndian();
    if (requestHeaderPrefix.version != 1 ||
        rpc.request.getLength() < sizeof(RequestHeaderVersion1)) {
        fail(std::move(rpc), Status::INVALID_VERSION);
        return;
    }
    auto& requestHeader = *static_cast<RequestHeaderVersion1*>(
                                          rpc.request.getData());
    requestHeader.fromBigEndian();

    // TODO(ongaro): If this is not the current cluster leader, need to
    // redirect the client.

    // Call the appropriate RPC handler based on the request's opCode.
    uint32_t skipBytes = uint32_t(sizeof(requestHeader));
    switch (requestHeader.opCode) {
        case OpCode::GET_SUPPORTED_RPC_VERSIONS:
            getSupportedRPCVersions(std::move(rpc), skipBytes);
            break;
        default:
            fail(std::move(rpc), Status::INVALID_REQUEST);
    }
}

/**
 * Place this at the top of each RPC handler. Afterwards, 'request' will refer
 * to the protocol buffer for the request with all required fields set.
 * 'response' will be an empty protocol buffer for you to fill in the response.
 */
#define PRELUDE(rpcClass) \
    ProtoBuf::ClientRPC::rpcClass::Request request; \
    if (!RPC::ProtoBuf::parse(rpc.request, request, skipBytes)) \
        fail(std::move(rpc), Status::INVALID_REQUEST); \
    ProtoBuf::ClientRPC::rpcClass::Response response;

////////// RPC handlers //////////

void
ClientService::getSupportedRPCVersions(ServerRPC rpc, uint32_t skipBytes)
{
    PRELUDE(GetSupportedRPCVersions);
    response.set_min_version(1);
    response.set_max_version(1);
    reply(std::move(rpc), response);
}

} // namespace LogCabin::Server
} // namespace LogCabin
