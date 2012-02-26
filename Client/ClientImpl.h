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
#include "Client/LeaderRPC.h"

#ifndef LOGCABIN_CLIENT_CLIENTIMPL_H
#define LOGCABIN_CLIENT_CLIENTIMPL_H

namespace LogCabin {
namespace Client {

/**
 * The implementation of the client library.
 * This is wrapped by Client::Cluster and Client::Log for usability.
 */
class ClientImpl {
  public:
    /// Constructor.
    ClientImpl();
    /**
     * Initialize this object. This must be called directly after the
     * constructor.
     * \param self
     *      This object needs a reference to itself so that it can keep itself
     *      alive while there are outstanding Log objects.
     * \param hosts
     *      A string describing the hosts in the cluster. This should be of the
     *      form host:port, where host is usually a DNS name that resolves to
     *      multiple IP addresses.
     * \param mockRPC
     *      This argument is used for unit testing only; some tests provide a
     *      LeaderRPCMock instance here that replaces #leaderRPC. In this case,
     *      'hosts' is ignored.
     */
    void init(std::weak_ptr<ClientImpl> self,
              const std::string& hosts,
              std::unique_ptr<LeaderRPCBase> mockRPC =
                    std::unique_ptr<LeaderRPCBase>());

    /// See Cluster::openLog.
    Log openLog(const std::string& logName);
    /// See Cluster::deleteLog.
    void deleteLog(const std::string& logName);
    /// See Cluster::listLogs.
    std::vector<std::string> listLogs();
    /// See Log::append and Log::invalidate.
    EntryId append(uint64_t logId, const Entry& entry, EntryId previousId);
    /// See Log::read.
    std::vector<Entry> read(uint64_t logId, EntryId from);
    /// See Log::getLastId.
    EntryId getLastId(uint64_t logId);
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

    /**
     * This is used to keep this object alive while there are outstanding
     * Log objects.
     */
    std::weak_ptr<ClientImpl> self;

    // ClientImpl is not copyable
    ClientImpl(const ClientImpl&) = delete;
    ClientImpl& operator=(const ClientImpl&) = delete;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENTIMPL_H */
