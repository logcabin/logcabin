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

#include <unistd.h>

#include "Client/LeaderRPC.h"
#include "Core/Debug.h"
#include "Core/Time.h"
#include "Protocol/Common.h"
#include "RPC/ClientSession.h"
#include "RPC/ClientRPC.h"

namespace LogCabin {
namespace Client {

//// class LeaderRPC::Call ////

LeaderRPC::Call::Call(LeaderRPC& leaderRPC)
    : leaderRPC(leaderRPC)
    , cachedSession()
    , rpc()
{
}

LeaderRPC::Call::~Call()
{
}

void
LeaderRPC::Call::start(OpCode opCode, const google::protobuf::Message& request)
{
    { // Save a reference to the leaderSession
        std::unique_lock<std::mutex> lockGuard(leaderRPC.mutex);
        cachedSession = leaderRPC.leaderSession;
    }
    rpc = RPC::ClientRPC(cachedSession,
                         Protocol::Common::ServiceId::CLIENT_SERVICE,
                         1,
                         opCode,
                         request);
}

void
LeaderRPC::Call::cancel()
{
    rpc.cancel();
    cachedSession.reset();
}

bool
LeaderRPC::Call::wait(google::protobuf::Message& response)
{
    typedef RPC::ClientRPC::Status Status;
    Protocol::Client::Error serviceSpecificError;
    Status status = rpc.waitForReply(&response, &serviceSpecificError,
                                     RPC::ClientRPC::TimePoint::max());

    // Decode the response
    switch (status) {
        case Status::OK:
            return true;
        case Status::SERVICE_SPECIFIC_ERROR:
            leaderRPC.handleServiceSpecificError(cachedSession,
                                                 serviceSpecificError);
            return false;
        case Status::RPC_FAILED:
            // If the session is broken, get a new one and try again.
            leaderRPC.connectRandom(cachedSession);
            return false;
        case Status::RPC_CANCELED:
            return false;
        case Status::TIMEOUT:
            PANIC("Unexpected RPC timeout");
    }
    PANIC("Unexpected RPC status");
}


//// class LeaderRPC ////

LeaderRPC::LeaderRPC(const RPC::Address& hosts)
    : windowCount(5)
    , windowNanos(1000 * 1000 * 100)
    , hosts(hosts)
    , eventLoop()
    , eventLoopThread(&Event::Loop::runForever, &eventLoop)
    , mutex()
    , leaderSession() // set by connect()
    , lastConnectTimes()
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    for (uint64_t i = 0; i < windowCount; ++i)
        lastConnectTimes.push_back(0);
    this->hosts.refresh(RPC::Address::TimePoint::max());
    connect(this->hosts, lockGuard);
}

LeaderRPC::~LeaderRPC()
{
    leaderSession.reset();
    eventLoop.exit();
    if (eventLoopThread.joinable())
        eventLoopThread.join();
}

void
LeaderRPC::call(OpCode opCode,
                const google::protobuf::Message& request,
                google::protobuf::Message& response)
{
    while (true) {
        Call c(*this);
        c.start(opCode, request);
        if (c.wait(response))
            return;
    }
}

std::unique_ptr<LeaderRPCBase::Call>
LeaderRPC::makeCall()
{
    return std::unique_ptr<LeaderRPCBase::Call>(new LeaderRPC::Call(*this));
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
                VERBOSE("Trying suggested %s as new leader (was using %s)",
                        error.leader_hint().c_str(),
                        cachedSession->toString().c_str());
                connectHost(error.leader_hint(), cachedSession);
            } else {
                // Well, this server isn't the leader. Try someone else.
                VERBOSE("Trying random host as new leader (was using %s)",
                        cachedSession->toString().c_str());
                connectRandom(cachedSession);
            }
            break;
        case Protocol::Client::Error::SESSION_EXPIRED:
            PANIC("Session expired");
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
    uint64_t nowNanos = Core::Time::getTimeNanos();
    if (lastConnectTimes.front() >  nowNanos - windowNanos) {
        usleep(unsigned(
            std::min(lastConnectTimes.front() + windowNanos - nowNanos,
                     windowNanos) / 1000));
        nowNanos = Core::Time::getTimeNanos();
    }
    lastConnectTimes.pop_front();
    lastConnectTimes.push_back(nowNanos);
    leaderSession = RPC::ClientSession::makeSession(
                        eventLoop,
                        address,
                        Protocol::Common::MAX_MESSAGE_LENGTH,
                        RPC::ClientSession::TimePoint::max());
}

void
LeaderRPC::connectRandom(std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession) {
        // Hope the next random host is the leader.
        // If that turns out to be false, we will soon find out.
        hosts.refresh(RPC::Address::TimePoint::max());
        connect(hosts, lockGuard);
    }
}

void
LeaderRPC::connectHost(const std::string& host,
                       std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession) {
        RPC::Address address(host, Protocol::Common::DEFAULT_PORT);
        address.refresh(RPC::Address::TimePoint::max());
        connect(address, lockGuard);
    }
}

} // namespace LogCabin::Client
} // namespace LogCabin
