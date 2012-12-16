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

#include <mutex>
#include <set>

#include "Client/Client.h"
#include "Client/ClientImplBase.h"
#include "Client/LeaderRPC.h"

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

    // Implementations of ClientImplBase methods
    void initDerived();
    Log openLog(const std::string& logName);
    void deleteLog(const std::string& logName);
    std::vector<std::string> listLogs();
    EntryId append(uint64_t logId, const Entry& entry, EntryId expectedId);
    std::vector<Entry> read(uint64_t logId, EntryId from);
    EntryId getLastId(uint64_t logId);
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
         * Call this before sending an RPC.
         */
        Protocol::Client::ExactlyOnceRPCInfo getRPCInfo();
        /**
         * Call this after receiving an RPCs response.
         */
        void doneWithRPC(const Protocol::Client::ExactlyOnceRPCInfo&);

      private:
        /**
         * Used to open a session with the cluster.
         * const and non-NULL except for unit tests.
         */
        ClientImpl* client;
        /**
         * Protects all the members of this class.
         */
        std::mutex mutex;
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
