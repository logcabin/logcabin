/* Copyright (c) 2012-2014 Stanford University
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

#include <memory>
#include <set>
#include <string>
#include <thread>

#include "include/LogCabin/Client.h"
#include "Client/Backoff.h"
#include "Client/LeaderRPC.h"
#include "Core/ConditionVariable.h"
#include "Core/Config.h"
#include "Core/Mutex.h"
#include "Core/Time.h"
#include "Event/Loop.h"

#ifndef LOGCABIN_CLIENT_CLIENTIMPL_H
#define LOGCABIN_CLIENT_CLIENTIMPL_H

namespace LogCabin {
namespace Client {

/**
 * A predicate on tree operations.
 * First component: the absolute path corresponding to the 'path'
 * argument of setCondition(), or empty if no condition is set.
 * Second component: the file contents given as the 'value' argument of
 * setCondition().
 */
typedef std::pair<std::string, std::string> Condition;

/**
 * The implementation of the client library.
 * This is wrapped by Client::Cluster and Client::Log for usability.
 */
class ClientImpl {
  public:
    /// Clock used for timeouts.
    typedef LeaderRPC::Clock Clock;
    /// Type for absolute time values used for timeouts.
    typedef LeaderRPC::TimePoint TimePoint;

    /// Constructor.
    explicit ClientImpl(const std::map<std::string, std::string>& options =
                            std::map<std::string, std::string>());
    /// Destructor.
    virtual ~ClientImpl();

    /**
     * Initialize this object. This must be called directly after the
     * constructor.
     * \param hosts
     *      A string describing the hosts in the cluster. This should be of the
     *      form host:port, where host is usually a DNS name that resolves to
     *      multiple IP addresses, or a semicolon-delimited list.
     */
    void init(const std::string& hosts);

    /**
     * Called by init() to do any necessary initialization of the derived
     * class.
     */
    virtual void initDerived();

    std::pair<uint64_t, Configuration> getConfiguration();
    ConfigurationResult setConfiguration(
                            uint64_t oldId,
                            const Configuration& newConfiguration);

    /// See Cluster::getServerInfo.
    Result getServerInfo(const std::string& host,
                         TimePoint timeout,
                         Server& info);

    /// See Cluster::getServerStats.
    Result getServerStats(const std::string& host,
                          TimePoint timeout,
                          Protocol::ServerStats& stats);

    /**
     * Return the canonicalized path name resulting from accessing path
     * relative to workingDirectory.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is relative and workingDirectory is not
     *         an absolute path.
     *       - INVALID_ARGUMENT if path attempts to access above the root
     *         directory.
     */
    Result canonicalize(const std::string& path,
                        const std::string& workingDirectory,
                        std::string& canonical);

    /// See Tree::makeDirectory.
    Result makeDirectory(const std::string& path,
                         const std::string& workingDirectory,
                         const Condition& condition,
                         TimePoint timeout);

    /// See Tree::listDirectory.
    Result listDirectory(const std::string& path,
                         const std::string& workingDirectory,
                         const Condition& condition,
                         TimePoint timeout,
                         std::vector<std::string>& children);

    /// See Tree::removeDirectory.
    Result removeDirectory(const std::string& path,
                           const std::string& workingDirectory,
                           const Condition& condition,
                           TimePoint timeout);

    /// See Tree::write.
    Result write(const std::string& path,
                 const std::string& workingDirectory,
                 const std::string& contents,
                 const Condition& condition,
                 TimePoint timeout);

    /// See Tree::read.
    Result read(const std::string& path,
                const std::string& workingDirectory,
                const Condition& condition,
                TimePoint timeout,
                std::string& contents);

    /// See Tree::removeFile.
    Result removeFile(const std::string& path,
                      const std::string& workingDirectory,
                      const Condition& condition,
                      TimePoint timeout);


  protected:

    /**
     * Asks the cluster leader for the range of supported RPC protocol
     * versions, and select the best one. This is used to make sure the client
     * and server are speaking the same version of the RPC protocol.
     */
    uint32_t negotiateRPCVersion();

    /**
     * Options/settings.
     */
    const Core::Config config;

    /**
     * The Event::Loop used to drive the underlying RPC mechanism.
     */
    Event::Loop eventLoop;

    /**
     * Used to rate-limit the creation of ClientSession objects (TCP
     * connections).
     */
    Backoff sessionCreationBackoff;

    /**
     * Describes the hosts in the cluster.
     */
    std::string hosts;

    /**
     * Used to send RPCs to the leader of the LogCabin cluster.
     */
    std::unique_ptr<LeaderRPCBase> leaderRPC;

    /**
     * The version of the RPC protocol to use when speaking to the cluster
     * leader. (This is the result of negotiateRPCVersion().)
     */
    uint32_t rpcProtocolVersion;

    /**
     * This class helps with providing exactly-once semantics for read-write
     * RPCs. For example, it assigns sequence numbers to RPCs, which servers
     * then use to prevent duplicate processing of duplicate requests.
     *
     * This class is implemented in a monitor style.
     */
    class ExactlyOnceRPCHelper {
      public:
        /**
         * Constructor.
         * \param client
         *     Used to open a session with the cluster. Should not be NULL.
         */
        explicit ExactlyOnceRPCHelper(ClientImpl* client);
        /**
         * Destructor.
         */
        ~ExactlyOnceRPCHelper();
        /**
         * Prepare to shut down (join with thread).
         */
        void exit();
        /**
         * Call this before sending an RPC.
         * \param timeout
         *      If this timeout elapses before a session can be opened with the
         *      cluster, this method will return early and the returned
         *      information will have a client_id set to 0, which is not a
         *      valid ID.
         * \return
         *      Info to be used with read-write RPCs, or if the timeout
         *      elapsed, a client_id set to 0.
         */
        Protocol::Client::ExactlyOnceRPCInfo getRPCInfo(TimePoint timeout);
        /**
         * Call this after receiving an RPCs response.
         */
        void doneWithRPC(const Protocol::Client::ExactlyOnceRPCInfo&);

      private:

        /**
         * Internal version of getRPCInfo() to avoid deadlock with self.
         */
        Protocol::Client::ExactlyOnceRPCInfo getRPCInfo(
            std::unique_lock<Core::Mutex>& lockGuard,
            TimePoint timeout);
        /**
         * Internal version of doneWithRPC() to avoid deadlock with self.
         */
        void doneWithRPC(const Protocol::Client::ExactlyOnceRPCInfo&,
                         std::unique_lock<Core::Mutex>& lockGuard);
        /**
         * Main function for keep-alive thread. Periodically makes
         * requests to the cluster to keep the client's session active.
         */
        void keepAliveThreadMain();

        /**
         * Used to open a session with the cluster.
         * const and non-NULL except for unit tests.
         */
        ClientImpl* client;
        /**
         * Protects all the members of this class.
         */
        mutable Core::Mutex mutex;
        /**
         * The numbers of the RPCs for which this client is still awaiting a
         * response.
         */
        std::set<uint64_t> outstandingRPCNumbers;
        /**
         * The client's session ID as returned by the open session RPC, or 0 if
         * one has not yet been assigned.
         */
        uint64_t clientId;
        /**
         * The number to assign to the next RPC.
         */
        uint64_t nextRPCNumber;
        /**
         * keepAliveThread blocks on this. Notified when lastKeepAliveStart,
         * keepAliveIntervalMs, or exiting changes.
         */
        Core::ConditionVariable keepAliveCV;
        /**
         * Flag to keepAliveThread that it should shut down.
         */
        bool exiting;
        /**
         * Time just before the last keep-alive or read-write request to the
         * cluster was made. The next keep-alive request will be invoked
         * keepAliveIntervalMs after this, if no intervening requests are made.
         */
        TimePoint lastKeepAliveStart;
        /**
         * How often session keep-alive requests are sent during periods of
         * inactivity, in milliseconds.
         */
        uint64_t keepAliveIntervalMs;

        /**
         * If set, this is an ongoing keep-alive RPC. This call is canceled to
         * interrupt #keepAliveThread when exiting.
         */
        std::unique_ptr<LeaderRPCBase::Call> keepAliveCall;

        /**
         * Runs keepAliveThreadMain().
         * Since this thread would be unexpected/wasteful for clients that only
         * issue read-only requests (or no requests at all), it is spawned
         * lazily, if/when the client opens its session with the cluster (upon
         * its first read-write request).
         */
        std::thread keepAliveThread;

        // ExactlyOnceRPCHelper is not copyable.
        ExactlyOnceRPCHelper(const ExactlyOnceRPCHelper&) = delete;
        ExactlyOnceRPCHelper& operator=(const ExactlyOnceRPCHelper&) = delete;
    } exactlyOnceRPCHelper;

    /**
     * A thread that runs the Event::Loop.
     */
    std::thread eventLoopThread;

    // ClientImpl is not copyable
    ClientImpl(const ClientImpl&) = delete;
    ClientImpl& operator=(const ClientImpl&) = delete;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENTIMPL_H */
