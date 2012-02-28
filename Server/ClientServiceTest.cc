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

#include <gtest/gtest.h>

#include "include/ProtoBuf.h"
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
using Protocol::Client::Status;
using Protocol::Client::RequestHeaderPrefix;
using Protocol::Client::RequestHeaderVersion1;
using Protocol::Client::ResponseHeaderPrefix;
using Protocol::Client::ResponseHeaderVersion1;
using RPC::Buffer;

class ServerClientServiceTest : public ::testing::Test {
    ServerClientServiceTest()
        : globals()
        , service(globals)
        , reply()
    {
    }

    Status rpc(RPC::Buffer request) {
        RPC::ServerRPC mockRPC;
        mockRPC.request = std::move(request);
        mockRPC.responseTarget = &reply;
        service.handleRPC(std::move(mockRPC));
        EXPECT_GE(reply.getLength(), sizeof(ResponseHeaderPrefix));
        ResponseHeaderPrefix prefix =
            *static_cast<const ResponseHeaderPrefix*>(reply.getData());
        prefix.fromBigEndian();
        return Status(prefix.status);
    }

    /**
     * This is intended for testing individual RPC handlers. It assumes
     * the server returns status OK and a real response.
     */
    void rpc(OpCode opCode,
             const google::protobuf::Message& request,
             google::protobuf::Message& response) {
        RPC::Buffer requestBuffer;
        RPC::ProtoBuf::serialize(request, requestBuffer,
                                 sizeof(RequestHeaderVersion1));
        RequestHeaderVersion1& header = *static_cast<RequestHeaderVersion1*>(
                                            requestBuffer.getData());
        header.prefix.version = 1;
        header.prefix.toBigEndian();
        header.opCode = opCode;
        header.toBigEndian();
        EXPECT_EQ(Status::OK, rpc(std::move(requestBuffer)));
        EXPECT_TRUE(RPC::ProtoBuf::parse(reply, response,
                                         sizeof(ResponseHeaderVersion1)));
    }

    Globals globals;
    ClientService service;
    Buffer reply;
};

TEST_F(ServerClientServiceTest, handleRPCPrefixTooShort) {
    EXPECT_EQ(Status::INVALID_VERSION, rpc(Buffer()));
}

TEST_F(ServerClientServiceTest, handleRPCInvalidVersion) {
    RequestHeaderVersion1 header;
    header.prefix.version = 2;
    header.prefix.toBigEndian();
    header.toBigEndian();
    EXPECT_EQ(Status::INVALID_VERSION,
              rpc(Buffer(&header, sizeof(header), NULL)));
}

TEST_F(ServerClientServiceTest, handleRPCHeaderTooShort) {
    RequestHeaderPrefix prefix;
    prefix.version = 1;
    prefix.toBigEndian();
    EXPECT_EQ(Status::INVALID_VERSION,
              rpc(Buffer(&prefix, sizeof(prefix), NULL)));
}

TEST_F(ServerClientServiceTest, handleRPCNormal) {
    RequestHeaderVersion1 header;
    header.prefix.version = 1;
    header.prefix.toBigEndian();
    header.opCode = OpCode::GET_SUPPORTED_RPC_VERSIONS;
    header.toBigEndian();
    EXPECT_EQ(Status::OK,
              rpc(Buffer(&header, sizeof(header), NULL)));
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Response replyProto;
    EXPECT_TRUE(RPC::ProtoBuf::parse(reply, replyProto,
                                     sizeof(ResponseHeaderVersion1)));
    EXPECT_EQ(1U, replyProto.min_version());
    EXPECT_EQ(1U, replyProto.max_version());
}

TEST_F(ServerClientServiceTest, handleRPCBadOpcode) {
    RequestHeaderVersion1 header;
    header.prefix.version = 1;
    header.prefix.toBigEndian();
    header.opCode = 255; // this is supposed to be an unassigned opCode
    header.toBigEndian();
    EXPECT_EQ(Status::INVALID_REQUEST,
              rpc(Buffer(&header, sizeof(header), NULL)));
}

////////// Tests for individual RPCs //////////

TEST_F(ServerClientServiceTest, getSupportedRPCVersions) {
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Request request;
    ProtoBuf::ClientRPC::GetSupportedRPCVersions::Response response;
    rpc(OpCode::GET_SUPPORTED_RPC_VERSIONS, request, response);
    EXPECT_EQ("min_version: 1"
              "max_version: 1", response);
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
