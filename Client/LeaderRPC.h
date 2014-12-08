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

#include <cinttypes>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "build/Protocol/Client.pb.h"
#include "Event/Loop.h"
#include "RPC/Address.h"

#ifndef LOGCABIN_CLIENT_LEADERRPC_H
#define LOGCABIN_CLIENT_LEADERRPC_H

namespace LogCabin {

// forward declaration
namespace RPC {
class ClientSession;
}

namespace Client {

/**
 * This class is used to send RPCs from clients to the leader of the LogCabin
 * cluster. It automatically finds and connects to the leader and transparently
 * rolls over to a new leader when necessary.
 *
 * There are two implementations of this interface: LeaderRPC is probably the
 * one you're interested in. LeaderRPCMock is used for unit testing only.
 */
class LeaderRPCBase {
  public:
    /**
     * RPC operation code.
     */
    typedef Protocol::Client::OpCode OpCode;

    /// Constructor.
    LeaderRPCBase() {}

    /// Destructor.
    virtual ~LeaderRPCBase() {}

    /**
     * Execute an RPC on the cluster leader.
     * This class guarantees that the RPC will be executed at least once.
     * \param opCode
     *      RPC operation code. The caller must guarantee that this is a valid
     *      opCode. (If the server rejects it, this will PANIC.)
     * \param request
     *      The parameters for the operation. The caller must guarantee that
     *      this is a well-formed request. (If the server rejects it, this will
     *      PANIC.)
     * \param[out] response
     *      The response to the operation will be filled in here.
     */
    virtual void call(OpCode opCode,
                      const google::protobuf::Message& request,
                      google::protobuf::Message& response) = 0;

    // LeaderRPCBase is not copyable
    LeaderRPCBase(const LeaderRPCBase&) = delete;
    LeaderRPCBase& operator=(const LeaderRPCBase&) = delete;
};

/**
 * This is the implementation of LeaderRPCBase that uses the RPC system.
 * (The other implementation, LeaderRPCMock, is only used for testing.)
 */
class LeaderRPC : public LeaderRPCBase {
  public:
    /**
     * Constructor.
     * \param hosts
     *      Describe the servers to connect to. This class assumes that
     *      refreshing 'hosts' will result in a random host that might be the
     *      current cluster leader.
     */
    explicit LeaderRPC(const RPC::Address& hosts);

    /// Destructor.
    ~LeaderRPC();

    void call(OpCode opCode,
              const google::protobuf::Message& request,
              google::protobuf::Message& response);
  private:

    /**
     * A helper for call() that decodes errors thrown by the service.
     */
    void handleServiceSpecificError(
        std::shared_ptr<RPC::ClientSession> cachedSession,
        const Protocol::Client::Error& error);

    /**
     * Connect to a new host in hopes that it is the cluster leader.
     * \param address
     *      The host to connect to.
     * \param lockGuard
     *      Proof that the caller is holding #mutex.
     */
    void
    connect(const RPC::Address& address,
            std::unique_lock<std::mutex>& lockGuard);

    /**
     * Connect to a random host in #hosts in hopes that it is the cluster
     * leader.
     * \param cachedSession
     *      This operation will only disconnect the current session if it is
     *      the same as the session that is provided here. This is used to
     *      detect races in which some other thread has already solved the
     *      problem.
     */
    void
    connectRandom(std::shared_ptr<RPC::ClientSession> cachedSession);

    /**
     * Connect to a specific host in hopes that it is the cluster leader.
     * \param host
     *      A string describing the host to connect to. This is passed in
     *      string form rather than as an Address, which might save a DNS
     *      lookup in case cachedSession turns out to be stale.
     * \param cachedSession
     *      This operation will only disconnect the current session if it is
     *      the same as the session that is provided here. This is used to
     *      detect races in which some other thread has already solved the
     *      problem.
     */
    void
    connectHost(const std::string& host,
                std::shared_ptr<RPC::ClientSession> cachedSession);

    /**
     * As a backoff mechanism, at most #windowCount connections are allowed in
     * any #windowNanos period of time.
     */
    const uint64_t windowCount;

    /**
     * As a backoff mechanism, at most #windowCount connections are allowed in
     * any #windowNanos period of time.
     */
    const uint64_t windowNanos;

    /**
     * An address referring to the hosts in the LogCabin cluster. A random host
     * is selected from here when this class doesn't know who the cluster
     * leader is.
     */
    RPC::Address hosts;

    /**
     * The Event::Loop used to drive the underlying RPC mechanism.
     */
    Event::Loop eventLoop;

    /**
     * A thread that runs the Event::Loop.
     */
    std::thread eventLoopThread;

    /**
     * Protects #leaderSession and #lastConnectTimes.
     * Threads hang on to this mutex while initiating new sessions to possible
     * cluster leaders, in case other threads are already handling the problem.
     */
    std::mutex mutex;

    /**
     * The goal is to get this session connected to the cluster leader.
     * This is never null, but it might sometimes point to the wrong host.
     */
    std::shared_ptr<RPC::ClientSession> leaderSession;

    /**
     * The time in nanoseconds since the Unix epoch when the last #windowCount
     * connections were initiated. If fewer than #windowCount connections have
     * been initiated, this is padded with zeros. The first time is the
     * oldest, and the last is the most recent.
     */
    std::deque<uint64_t> lastConnectTimes;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_LEADERRPC_H */
