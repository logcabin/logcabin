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

#include <memory>
#include <string>
#include "Client/Client.h"

#ifndef LOGCABIN_CLIENT_CLIENTIMPLBASE_H
#define LOGCABIN_CLIENT_CLIENTIMPLBASE_H

namespace LogCabin {
namespace Client {

/**
 * A base class for the implementation of the client library.
 * This is implemented by Client::ClientImpl and Client::MockClientImpl.
 */
class ClientImplBase {
  public:
    /// Constructor.
    ClientImplBase();

    /// Destructor.
    virtual ~ClientImplBase();

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
     */
    void init(std::weak_ptr<ClientImplBase> self, const std::string& hosts);

    /**
     * Called by init() to do any necessary initialization of the derived
     * class.
     */
    virtual void initDerived();

    /// See Cluster::openLog.
    virtual Log openLog(const std::string& logName) = 0;
    /// See Cluster::deleteLog.
    virtual void deleteLog(const std::string& logName) = 0;
    /// See Cluster::listLogs.
    virtual std::vector<std::string> listLogs() = 0;
    /// See Log::append and Log::invalidate.
    virtual EntryId append(uint64_t logId, const Entry& entry,
                           EntryId expectedId) = 0;
    /// See Log::read.
    virtual std::vector<Entry> read(uint64_t logId, EntryId from) = 0;
    /// See Log::getLastId.
    virtual EntryId getLastId(uint64_t logId) = 0;

    virtual std::pair<uint64_t, Configuration> getConfiguration() = 0;
    virtual ConfigurationResult setConfiguration(
                uint64_t oldId,
                const Configuration& newConfiguration) = 0;

  protected:
    /**
     * This is used to keep this object alive while there are outstanding
     * Log objects.
     */
    std::weak_ptr<ClientImplBase> self;
    /**
     * Describes the hosts in the cluster.
     */
    std::string hosts;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENTIMPLBASE_H */
