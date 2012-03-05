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

#include <deque>
#include <queue>
#include <memory>
#include <utility>

#include "Core/ProtoBuf.h"
#include "Client/LeaderRPC.h"

#ifndef LOGCABIN_CLIENT_LEADERRPCMOCK_H
#define LOGCABIN_CLIENT_LEADERRPCMOCK_H

namespace LogCabin {
namespace Client {

/**
 * This class is used in unit testing to interpose when clients send RPCs to
 * the leader of the LogCabin cluster.
 */
class LeaderRPCMock : public LeaderRPCBase {
  public:
    typedef std::unique_ptr<google::protobuf::Message> MessagePtr;
    /// Constructor.
    LeaderRPCMock();
    /// Destructor.
    ~LeaderRPCMock();
    /**
     * Expect the next request operation to have type opCode, and return it
     * the given response.
     */
    void expect(OpCode opCode,
                const google::protobuf::Message& response);
    /**
     * Pop the first request from the queue.
     */
    MessagePtr popRequest();

    /**
     * Mocks out an RPC call.
     * You should have called expect prior to this to prime a response.
     * The request will be logged so you can pop it.
     */
    void call(OpCode opCode,
              const google::protobuf::Message& request,
              google::protobuf::Message& response);
  private:
    /**
     * A queue of requests that have come in from call().
     */
    std::deque<std::pair<OpCode, MessagePtr>> requestLog;
    /**
     * A queue of responses that have been primed from expect().
     */
    std::queue<std::pair<OpCode, MessagePtr>> responseQueue;
};

} // namespace Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_LEADERRPC_H */
