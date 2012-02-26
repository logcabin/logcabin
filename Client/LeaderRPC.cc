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

#include "include/Debug.h"
#include "Client/LeaderRPC.h"
#include "RPC/Buffer.h"
#include "RPC/ClientSession.h"
#include "RPC/ProtoBuf.h"
#include "proto/Client.h"

namespace LogCabin {
namespace Client {

using Protocol::Client::RequestHeaderVersion1;
using Protocol::Client::ResponseHeaderVersion1;
using Protocol::Client::Status;

LeaderRPC::LeaderRPC(const RPC::Address& hosts)
    : hosts(hosts)
    , eventLoop()
    , eventLoopThread(&Event::Loop::runForever, &eventLoop)
    , mutex()
    , leaderSession() // set by connect()
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    connect(hosts, lockGuard);
}

LeaderRPC::~LeaderRPC()
{
    leaderSession.reset();
    eventLoop.exit();
    eventLoopThread.join();
}

void
LeaderRPC::call(OpCode opCode,
                const google::protobuf::Message& request,
                google::protobuf::Message& response)
{
    // TODO(ongaro): Rate limit the retries so as not to overwhelm servers
    // while they're choosing a new leader, etc.
    while (true) {

        // Serialize the request into a RPC::Buffer
        RPC::Buffer requestBuffer;
        RPC::ProtoBuf::serialize(request, requestBuffer,
                                 sizeof(RequestHeaderVersion1));
        auto& requestHeader =
            *static_cast<RequestHeaderVersion1*>(requestBuffer.getData());
        requestHeader.version = 1;
        requestHeader.opCode = opCode;
        requestHeader.toBigEndian();

        // Save a reference to the leaderSession
        std::shared_ptr<RPC::ClientSession> cachedSession;
        {
            std::unique_lock<std::mutex> lockGuard(mutex);
            cachedSession = leaderSession;
        }

        // Send the request and wait for the RPC response
        RPC::ClientRPC rpc =
            cachedSession->sendRequest(std::move(requestBuffer));
        rpc.waitForReply();

        // If the session is broken, get a new one and try again.
        std::string rpcError = rpc.getErrorMessage();
        if (!rpcError.empty()) {
            LOG(WARNING, "RPC failed: %s. Searching for leader.",
                rpcError.c_str());
            // This should indicate the session has failed. Get a new one.
            connectRandom(cachedSession);
            continue;
        }

        // Extract the response's status field.
        RPC::Buffer responseBuffer = rpc.extractReply();
        auto& responseHeader =
            *static_cast<ResponseHeaderVersion1*>(responseBuffer.getData());
        responseHeader.fromBigEndian();
        switch (responseHeader.status) {

            // The RPC succeeded. Parse the response into a protocol buffer.
            case Status::OK:
                if (!RPC::ProtoBuf::parse(responseBuffer, response,
                                          sizeof(responseHeader))) {
                    PANIC("Could not parse server response");
                }
                return;

            // The server doesn't understand this version of the header
            // protocol. Since this library only runs version 1 of the
            // protocol, this shouldn't happen if servers continue supporting
            // version 1.
            case Status::INVALID_VERSION: {
                PANIC("This client is too old to talk to your "
                      "LogCabin cluster. You'll need to update your "
                      "LogCabin client library.");
                break;
            }

            // The server disliked our request. This shouldn't happen because
            // the higher layers of software were supposed to negotiate an RPC
            // protocol version.
            case Status::INVALID_REQUEST:
                PANIC("The server found this request to be invalid.\n%s",
                      DLog::ProtoBuf::dumpString(request).c_str());

            // The server we tried is not the current cluster leader.
            case Status::NOT_LEADER: {
                if (responseBuffer.getLength() > sizeof(responseHeader)) {
                    // Server returned hint as to who the leader might be.
                    const char* hint =
                        (static_cast<const char*>(responseBuffer.getData()) +
                         sizeof(responseHeader));
                    LOG(DBG, "Trying suggested %s as new leader", hint);
                    connectHost(hint, cachedSession);
                } else {
                    // Well, this server isn't the leader. Try someone else.
                    LOG(DBG, "Trying random host as new leader");
                    connectRandom(cachedSession);
                }
                break;
            }

            default:
                // The server shouldn't reply back with status codes we don't
                // understand. That's why we gave it a version number in the
                // request header.
                PANIC("Unknown status %u returned from server after sending "
                      "it protocol version 1 in the request header. This "
                      "probably indicates a bug in the server.",
                      responseHeader.status);
        }
    }
}

void
LeaderRPC::connect(const RPC::Address& address,
                   std::unique_lock<std::mutex>& lockGuard)
{
    leaderSession = RPC::ClientSession::makeSession(
                        eventLoop,
                        address,
                        Protocol::Client::MAX_MESSAGE_LENGTH);
}

void
LeaderRPC::connectRandom(std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession) {
        // Hope the next random host is the leader.
        // If that turns out to be false, someone will soon find out.
        hosts.refresh();
        connect(hosts, lockGuard);
    }
}

void
LeaderRPC::connectHost(const char* host,
                       std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession)
        connect(RPC::Address(host, 0), lockGuard);
}

} // namespace LogCabin::Client
} // namespace LogCabin
