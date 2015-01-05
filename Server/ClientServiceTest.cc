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
#include <thread>

#include "build/Protocol/Client.pb.h"
#include "Core/ProtoBuf.h"
#include "Protocol/Common.h"
#include "RPC/Buffer.h"
#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"
#include "Server/Globals.h"

namespace LogCabin {
namespace Server {
namespace {

using Protocol::Client::OpCode;
typedef RPC::ClientRPC::Status Status;
typedef RPC::ClientRPC::TimePoint TimePoint;

class ServerClientServiceTest : public ::testing::Test {
    ServerClientServiceTest()
        : globals()
        , session()
        , thread()
    {
    }

    // Test setup is deferred for handleRPCBadOpcode, which needs to bind the
    // server port in a new thread.
    void init() {
        if (!globals) {
            globals.reset(new Globals());
            globals->config.set("storageModule", "memory");
            globals->config.set("uuid", "my-fake-uuid-123");
            globals->config.set("servers", "127.0.0.1");
            globals->init();
            RPC::Address address("127.0.0.1", Protocol::Common::DEFAULT_PORT);
            address.refresh(RPC::Address::TimePoint::max());
            session = RPC::ClientSession::makeSession(
                globals->eventLoop,
                address,
                1024 * 1024,
                TimePoint::max());
            thread = std::thread(&Globals::run, globals.get());
        }
    }

    ~ServerClientServiceTest()
    {
        if (globals) {
            globals->eventLoop.exit();
            thread.join();
        }
    }

    void
    call(OpCode opCode,
         const google::protobuf::Message& request,
         google::protobuf::Message& response)
    {
        RPC::ClientRPC rpc(session,
                           Protocol::Common::ServiceId::CLIENT_SERVICE,
                           1, opCode, request);
        EXPECT_EQ(Status::OK, rpc.waitForReply(&response, NULL,
                                               TimePoint::max()))
            << rpc.getErrorMessage();
    }

    std::unique_ptr<Globals> globals;
    std::shared_ptr<RPC::ClientSession> session;
    std::thread thread;
};

TEST_F(ServerClientServiceTest, handleRPCBadOpcode) {
    Protocol::Client::GetSupportedRPCVersions::Request request;
    Protocol::Client::GetSupportedRPCVersions::Response response;
    int bad = 255;
    OpCode unassigned = static_cast<OpCode>(bad);
    EXPECT_DEATH({init();
                  call(unassigned, request, response);},
                 "request.*invalid");
}

////////// Tests for individual RPCs //////////

TEST_F(ServerClientServiceTest, getSupportedRPCVersions) {
    init();
    Protocol::Client::GetSupportedRPCVersions::Request request;
    Protocol::Client::GetSupportedRPCVersions::Response response;
    call(OpCode::GET_SUPPORTED_RPC_VERSIONS, request, response);
    EXPECT_EQ("min_version: 1"
              "max_version: 1", response);
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
