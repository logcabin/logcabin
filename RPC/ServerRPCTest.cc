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

#include "build/Core/ProtoBufTest.pb.h"
#include "Core/Buffer.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "RPC/OpaqueServerRPC.h"
#include "RPC/Protocol.h"
#include "RPC/ServerRPC.h"

namespace LogCabin {
namespace RPC {
namespace {

using RPC::Protocol::RequestHeaderVersion1;
using RPC::Protocol::ResponseHeaderVersion1;
using RPC::Protocol::Status;

class RPCServerRPCTest : public ::testing::Test {
    RPCServerRPCTest()
        : request()
        , response()
        , serverRPC()
        , payload()
    {
        payload.set_field_a(3);
        payload.set_field_b(4);
    }

    void
    makeRequest(uint8_t version,
                uint16_t service,
                uint8_t serviceSpecificErrorVersion,
                uint16_t opCode,
                const google::protobuf::Message* payload)
    {
        if (payload == NULL) {
            request.setData(
                new RequestHeaderVersion1(),
                sizeof(RequestHeaderVersion1),
                Core::Buffer::deleteObjectFn<RequestHeaderVersion1*>);
        } else {
            Core::ProtoBuf::serialize(*payload, request,
                                      sizeof(RequestHeaderVersion1));
        }
        RequestHeaderVersion1& header =
            *static_cast<RequestHeaderVersion1*>(request.getData());
        header.prefix.version = version;
        header.prefix.toBigEndian();
        header.service = service;
        header.serviceSpecificErrorVersion = serviceSpecificErrorVersion;
        header.opCode = opCode;
        header.toBigEndian();
    }

    void
    call()
    {
        OpaqueServerRPC opaqueServerRPC;
        opaqueServerRPC.request = std::move(request);
        opaqueServerRPC.responseTarget = &response;
        serverRPC = ServerRPC(std::move(opaqueServerRPC));
    }

    Status
    getStatus()
    {
        EXPECT_GE(response.getLength(),
                  sizeof(ResponseHeaderVersion1));
        ResponseHeaderVersion1 header =
            *static_cast<const ResponseHeaderVersion1*>(response.getData());
        header.prefix.fromBigEndian();
        return header.prefix.status;
    }

    Core::Buffer request;
    Core::Buffer response;
    ServerRPC serverRPC;
    LogCabin::ProtoBuf::TestMessage payload;
};

TEST_F(RPCServerRPCTest, constructor_tooShort) {
    call();
    EXPECT_EQ(Status::INVALID_REQUEST, getStatus());
}

TEST_F(RPCServerRPCTest, constructor_badVersion) {
    makeRequest(2, 0, 0, 0, NULL);
    call();
    EXPECT_EQ(Status::INVALID_VERSION, getStatus());
}

TEST_F(RPCServerRPCTest, constructor_normal) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    EXPECT_EQ(2U, serverRPC.getService());
    EXPECT_EQ(3U, serverRPC.getServiceSpecificErrorVersion());
    EXPECT_EQ(4U, serverRPC.getOpCode());
    EXPECT_TRUE(serverRPC.needsReply());
    serverRPC.closeSession();
}

// default constructor: nothing to test
// move constructor: nothing to test
// destructor: nothing to test
// move assignment: nothing to test

TEST_F(RPCServerRPCTest, getRequest_normal) {
    makeRequest(1, 2, 3, 4, &payload);
    call();
    LogCabin::ProtoBuf::TestMessage actual;
    EXPECT_TRUE(serverRPC.getRequest(actual));
    EXPECT_EQ(payload, actual);
    EXPECT_TRUE(serverRPC.needsReply());
    serverRPC.closeSession();
}

TEST_F(RPCServerRPCTest, getRequest_inactive) {
    makeRequest(1, 2, 3, 4, &payload);
    call();
    serverRPC.rejectInvalidRequest();
    LogCabin::ProtoBuf::TestMessage actual;
    EXPECT_FALSE(serverRPC.getRequest(actual));
    EXPECT_FALSE(serverRPC.needsReply());
}

TEST_F(RPCServerRPCTest, getRequest_invalid) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    LogCabin::ProtoBuf::TestMessage actual;
    LogCabin::Core::Debug::setLogPolicy({{"", "ERROR"}});
    EXPECT_FALSE(serverRPC.getRequest(actual));
    EXPECT_EQ(Status::INVALID_REQUEST, getStatus());
    EXPECT_FALSE(serverRPC.needsReply());
}

TEST_F(RPCServerRPCTest, reply) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    serverRPC.reply(payload);
    EXPECT_EQ(Status::OK, getStatus());
    EXPECT_FALSE(serverRPC.needsReply());
    LogCabin::ProtoBuf::TestMessage actual;
    EXPECT_TRUE(Core::ProtoBuf::parse(response,
                                      actual,
                                      sizeof(ResponseHeaderVersion1)));
    EXPECT_EQ(payload, actual);
}

TEST_F(RPCServerRPCTest, returnError) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    serverRPC.returnError(payload);
    EXPECT_EQ(Status::SERVICE_SPECIFIC_ERROR, getStatus());
    EXPECT_FALSE(serverRPC.needsReply());
    LogCabin::ProtoBuf::TestMessage actual;
    EXPECT_TRUE(Core::ProtoBuf::parse(response,
                                      actual,
                                      sizeof(ResponseHeaderVersion1)));
    EXPECT_EQ(payload, actual);
}

TEST_F(RPCServerRPCTest, rejectInvalidService) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    serverRPC.rejectInvalidService();
    EXPECT_EQ(Status::INVALID_SERVICE, getStatus());
    EXPECT_FALSE(serverRPC.needsReply());
}

TEST_F(RPCServerRPCTest, rejectInvalidRequest) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    serverRPC.rejectInvalidService();
    EXPECT_EQ(Status::INVALID_SERVICE, getStatus());
    EXPECT_FALSE(serverRPC.needsReply());
}

TEST_F(RPCServerRPCTest, closeSession) {
    makeRequest(1, 2, 3, 4, NULL);
    call();
    serverRPC.closeSession();
    EXPECT_FALSE(serverRPC.needsReply());
    EXPECT_FALSE(serverRPC.opaqueRPC.socket.lock());
}

// reject tested sufficiently by rejectInvalidService

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
