/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014-2015 Diego Ongaro
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

#include "Client/Backoff.h"
#include "Client/LeaderRPC.h"
#include "Core/Debug.h"
#include "Protocol/Common.h"
#include "RPC/ClientSession.h"
#include "RPC/ClientRPC.h"

namespace LogCabin {
namespace Client {

//// class LeaderRPCBase ////

std::ostream&
operator<<(std::ostream& os, const LeaderRPCBase::Status& status)
{
    switch (status) {
        case LeaderRPCBase::Status::OK:
            os << "Status::OK";
            break;
        case LeaderRPCBase::Status::TIMEOUT:
            os << "Status::TIMEOUT";
            break;
        case LeaderRPCBase::Status::INVALID_REQUEST:
            os << "Status::INVALID_REQUEST";
            break;
    }
    return os;
}

std::ostream&
operator<<(std::ostream& os, const LeaderRPCBase::Call::Status& status)
{
    switch (status) {
        case LeaderRPCBase::Call::Status::OK:
            os << "Status::OK";
            break;
        case LeaderRPCBase::Call::Status::RETRY:
            os << "Status::RETRY";
            break;
        case LeaderRPCBase::Call::Status::TIMEOUT:
            os << "Status::TIMEOUT";
            break;
        case LeaderRPCBase::Call::Status::INVALID_REQUEST:
            os << "Status::INVALID_REQUEST";
            break;
    }
    return os;
}

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
LeaderRPC::Call::start(OpCode opCode,
                       const google::protobuf::Message& request,
                       TimePoint timeout)
{
    // Save a reference to the leaderSession
    cachedSession = leaderRPC.getSession(timeout);
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

LeaderRPC::Call::Status
LeaderRPC::Call::wait(google::protobuf::Message& response,
                      TimePoint timeout)
{
    typedef RPC::ClientRPC::Status RPCStatus;
    Protocol::Client::Error error;
    RPCStatus status = rpc.waitForReply(&response, &error, timeout);

    // Decode the response
    switch (status) {
        case RPCStatus::OK:
            return Call::Status::OK;
        case RPCStatus::SERVICE_SPECIFIC_ERROR:
            switch (error.error_code()) {
                case Protocol::Client::Error::NOT_LEADER:
                    // The server we tried is not the current cluster leader.
                    if (error.has_leader_hint()) {
                        NOTICE("Will try suggested %s as new leader "
                               "(was using %s)",
                               error.leader_hint().c_str(),
                               cachedSession->toString().c_str());
                        leaderRPC.reportRedirect(cachedSession,
                                                 error.leader_hint());
                    } else {
                        NOTICE("Will try random host as new leader "
                               "(was using %s)",
                               cachedSession->toString().c_str());
                        leaderRPC.reportFailure(cachedSession);
                    }
                    break;
                default:
                    // Hmm, we don't know what this server is trying to tell
                    // us, but something is wrong. The server shouldn't reply
                    // back with error codes we don't understand. That's why we
                    // gave it a serverSpecificErrorVersion number in the
                    // request header.
                    PANIC("Unknown error code %u returned in service-specific "
                          "error. This probably indicates a bug in the server",
                          error.error_code());
            }
            break;
        case RPCStatus::RPC_FAILED:
            NOTICE("RPC failed: %s", cachedSession->toString().c_str());
            leaderRPC.reportFailure(cachedSession);
            break;
        case RPCStatus::RPC_CANCELED:
            break;
        case RPCStatus::TIMEOUT:
            return Call::Status::TIMEOUT;
        case RPCStatus::INVALID_SERVICE:
            PANIC("The server isn't running the ClientService");
        case RPCStatus::INVALID_REQUEST:
            return Call::Status::INVALID_REQUEST;
    }
    if (timeout < Clock::now())
        return Call::Status::TIMEOUT;
    else
        return Call::Status::RETRY;
}


//// class LeaderRPC ////

LeaderRPC::LeaderRPC(const RPC::Address& hosts,
                     Event::Loop& eventLoop,
                     SessionManager::ClusterUUID& clusterUUID,
                     Backoff& sessionCreationBackoff,
                     SessionManager& sessionManager)
    : eventLoop(eventLoop)
    , clusterUUID(clusterUUID)
    , sessionCreationBackoff(sessionCreationBackoff)
    , sessionManager(sessionManager)
    , mutex()
    , hosts(hosts)
    , leaderHint()
    , leaderSession() // set by connect()
{
}

LeaderRPC::~LeaderRPC()
{
    leaderSession.reset();
}

LeaderRPC::Status
LeaderRPC::call(OpCode opCode,
                const google::protobuf::Message& request,
                google::protobuf::Message& response,
                TimePoint timeout)
{
    while (true) {
        Call c(*this);
        c.start(opCode, request, timeout);
        Call::Status callStatus = c.wait(response, timeout);
        switch (callStatus) {
            case Call::Status::OK:
                return Status::OK;
            case Call::Status::TIMEOUT:
                return Status::TIMEOUT;
            case Call::Status::RETRY:
                break;
            case Call::Status::INVALID_REQUEST:
                return Status::INVALID_REQUEST;
        }
    }
}

std::unique_ptr<LeaderRPCBase::Call>
LeaderRPC::makeCall()
{
    return std::unique_ptr<LeaderRPCBase::Call>(new LeaderRPC::Call(*this));
}

std::shared_ptr<RPC::ClientSession>
LeaderRPC::getSession(TimePoint timeout)
{
    std::lock_guard<std::mutex> lockGuard(mutex);
    if (leaderSession)
        return leaderSession;

    // sleep if we've tried to connect too much recently
    sessionCreationBackoff.delayAndBegin(timeout);

    RPC::Address address;
    if (leaderHint.empty()) {
        // Hope the next random host is the leader.
        // If that turns out to be false, we will soon find out.
        hosts.refresh(timeout);
        address = hosts;
    } else {
        address = RPC::Address(leaderHint, Protocol::Common::DEFAULT_PORT);
        address.refresh(timeout);
        leaderHint.clear();
    }
    NOTICE("Connecting to: %s", address.toString().c_str());
    leaderSession = sessionManager.createSession(
        address, timeout, &clusterUUID);
    return leaderSession;
}

void
LeaderRPC::reportFailure(std::shared_ptr<RPC::ClientSession> cachedSession)
{
    std::lock_guard<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession)
        leaderSession.reset();
}

void
LeaderRPC::reportRedirect(std::shared_ptr<RPC::ClientSession> cachedSession,
                          const std::string& host)
{
    std::lock_guard<std::mutex> lockGuard(mutex);
    if (cachedSession == leaderSession) {
        leaderSession.reset();
        leaderHint = host;
    }
}

} // namespace LogCabin::Client
} // namespace LogCabin
