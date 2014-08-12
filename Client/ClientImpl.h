/* Copyright (c) 2012-2014 Stanford University
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

#include <mutex>
#include <set>

#include "Client/Client.h"
#include "Client/ClientImplBase.h"
#include "Client/LeaderRPC.h"
#include "Core/ConditionVariable.h"
#include "Core/Time.h"

#ifndef LOGCABIN_CLIENT_CLIENTIMPL_H
#define LOGCABIN_CLIENT_CLIENTIMPL_H

namespace LogCabin {
namespace Client {

/**
 * The implementation of the client library.
 * This is wrapped by Client::Cluster and Client::Log for usability.
 */
class ClientImpl : public ClientImplBase {
  public:
    /// Constructor.
    ClientImpl();
    /// Destructor.
    ~ClientImpl();

    // Implementations of ClientImplBase methods
    void initDerived();
    std::pair<uint64_t, Configuration> getConfiguration();
    ConfigurationResult setConfiguration(
                            uint64_t oldId,
                            const Configuration& newConfiguration);
    Result canonicalize(const std::string& path,
                        const std::string& workingDirectory,
                        std::string& canonical);
    Result makeDirectory(const std::string& path,
                         const std::string& workingDirectory,
                         const Condition& condition);
    Result listDirectory(const std::string& path,
                         const std::string& workingDirectory,
                         const Condition& condition,
                         std::vector<std::string>& children);
    Result removeDirectory(const std::string& path,
                           const std::string& workingDirectory,
                           const Condition& condition);
    Result write(const std::string& path,
                 const std::string& workingDirectory,
                 const std::string& contents,
                 const Condition& condition);
    Result read(const std::string& path,
                const std::string& workingDirectory,
                const Condition& condition,
                std::string& contents);
    Result removeFile(const std::string& path,
                      const std::string& workingDirectory,
                      const Condition& condition);


  protected:

    /**
     * Make no-op request to the cluster to keep the client's session active.
     */
    void keepAlive();

    /**
     * Asks the cluster leader for the range of supported RPC protocol
     * versions, and select the best one. This is used to make sure the client
     * and server are speaking the same version of the RPC protocol.
     */
    uint32_t negotiateRPCVersion();

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
     * This class helps with providing exactly-once semantics for RPCs. For
     * example, it assigns sequence numbers to RPCs, which servers then use to 
     * prevent duplicate processing of duplicate requests.
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
         */
        Protocol::Client::ExactlyOnceRPCInfo getRPCInfo();
        /**
         * Call this after receiving an RPCs response.
         */
        void doneWithRPC(const Protocol::Client::ExactlyOnceRPCInfo&);

      private:
        /**
         * Main function for keep-alive thread. Periodically makes
         * requests to the cluster to keep the client's session active.
         */
        void keepAliveThreadMain();

        /**
         * Clock type used for keep-alive timer.
         */
        typedef Core::Time::SteadyClock Clock;
        /**
         * TimePoint type used for keep-alive timer.
         */
        typedef Clock::time_point TimePoint;

        /**
         * Used to open a session with the cluster.
         * const and non-NULL except for unit tests.
         */
        ClientImpl* client;
        /**
         * Protects all the members of this class.
         */
        mutable std::mutex mutex;
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
         * Runs keepAliveThreadMain().
         * Since this thread would be unexpected/wasteful for clients that only
         * issue read-only requests (or no requests at all), it is spawned
         * lazy, if/when the client opens its session with the cluster (upon
         * its first read-write request).
         */
        std::thread keepAliveThread;

        // ExactlyOnceRPCHelper is not copyable.
        ExactlyOnceRPCHelper(const ExactlyOnceRPCHelper&) = delete;
        ExactlyOnceRPCHelper& operator=(const ExactlyOnceRPCHelper&) = delete;
    } exactlyOnceRPCHelper;

    // ClientImpl is not copyable
    ClientImpl(const ClientImpl&) = delete;
    ClientImpl& operator=(const ClientImpl&) = delete;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENTIMPL_H */
