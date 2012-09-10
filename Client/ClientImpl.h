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

  private:
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

    // ClientImpl is not copyable
    ClientImpl(const ClientImpl&) = delete;
    ClientImpl& operator=(const ClientImpl&) = delete;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENTIMPL_H */
