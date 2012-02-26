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

#include "include/Debug.h"
#include "Client/LeaderRPCMock.h"

namespace LogCabin {
namespace Client {

LeaderRPCMock::LeaderRPCMock()
    : requestLog()
    , responseQueue()
{
}

LeaderRPCMock::~LeaderRPCMock()
{
}

void
LeaderRPCMock::expect(OpCode opCode,
                      const google::protobuf::Message& response)
{
    MessagePtr responseCopy(response.New());
    responseCopy->CopyFrom(response);
    responseQueue.push({opCode, std::move(responseCopy)});
}

LeaderRPCMock::MessagePtr
LeaderRPCMock::popRequest()
{
    MessagePtr request = std::move(requestLog.at(0).second);
    requestLog.pop_front();
    return std::move(request);
}

void
LeaderRPCMock::call(OpCode opCode,
          const google::protobuf::Message& request,
          google::protobuf::Message& response)
{
    MessagePtr requestCopy(request.New());
    requestCopy->CopyFrom(request);
    requestLog.push_back({opCode, std::move(requestCopy)});
    ASSERT_LT(0U, responseQueue.size())
        << "The client sent an unexpected RPC:\n"
        << request.GetTypeName() << ":\n"
        << ProtoBuf::dumpString(request, false);
    auto& opCodeMsgPair = responseQueue.front();
    EXPECT_EQ(opCode, opCodeMsgPair.first);
    response.CopyFrom(*opCodeMsgPair.second);
    responseQueue.pop();
}

} // namespace LogCabin::Client
} // namespace LogCabin
