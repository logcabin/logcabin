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

#include "Client/LeaderRPC.h"
#include "Core/Debug.h"
#include "Protocol/Common.h"
#include "RPC/ClientSession.h"
#include "RPC/ClientRPC.h"

namespace LogCabin {
namespace Client {

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
    typedef RPC::ClientRPC::Status Status;

    // TODO(ongaro): Rate limit the retries so as not to overwhelm servers
    // while they're choosing a new leader, etc.
    while (true) {
        // Save a reference to the leaderSession
        std::shared_ptr<RPC::ClientSession> cachedSession;
        {
            std::unique_lock<std::mutex> lockGuard(mutex);
            cachedSession = leaderSession;
        }

        // Execute the RPC
        RPC::ClientRPC rpc(cachedSession,
                           Protocol::Common::ServiceId::CLIENT_SERVICE,
                           1,
                           opCode,
                           request);
        Protocol::Client::Error serviceSpecificError;
        Status status = rpc.waitForReply(&response, &serviceSpecificError);

        // Decode the response
        switch (status) {
            case Status::OK:
                return;
            case Status::SERVICE_SPECIFIC_ERROR:
                handleServiceSpecificError(cachedSession,
                                           serviceSpecificError);
                break;
            case Status::RPC_FAILED:
                // If the session is broken, get a new one and try again.
                connectRandom(cachedSession);
                break;
        }
    }
}

void
LeaderRPC::handleServiceSpecificError(
        std::shared_ptr<RPC::ClientSession> cachedSession,
        const Protocol::Client::Error& error)
{
    switch (error.error_code()) {
        case Protocol::Client::Error::NOT_LEADER:
            // The server we tried is not the current cluster leader.
            if (error.has_leader_hint()) {
                // Server returned hint as to who the leader might be.
                LOG(DBG, "Trying suggested %s as new leader",
                         error.leader_hint().c_str());
                connectHost(error.leader_hint(), cachedSession);
            } else {
                // Well, this server isn't the leader. Try someone else.
                LOG(DBG, "Trying random host as new leader");
                connectRandom(cachedSession);
            }
            break;
        default:
            // Hmm, we don't know what this server is trying to tell us, but
            // something is wrong. The server shouldn't reply back with error
            // codes we don't understand. That's why we gave it a
            // serverSpecificErrorVersion number in the request header.
            PANIC("Unknown error code %u returned in service-specific error. "
                  "This probably indicates a bug in the server.",
                  error.error_code());
    }
}

void
LeaderRPC::connect(const RPC::Address& address,
                   std::unique_lock<std::mutex>& lockGuard)
{
    leaderSession = RPC::ClientSession::makeSession(
                        eventLoop,
                        address,
                        Protocol::Common::MAX_MESSAGE_LENGTH);
}

void
LeaderRPC::connectRandom(std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession) {
        // Hope the next random host is the leader.
        // If that turns out to be false, we will soon find out.
        hosts.refresh();
        connect(hosts, lockGuard);
    }
}

void
LeaderRPC::connectHost(const std::string& host,
                       std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession) {
        connect(RPC::Address(host, Protocol::Common::DEFAULT_PORT),
                lockGuard);
    }
}

} // namespace LogCabin::Client
} // namespace LogCabin
