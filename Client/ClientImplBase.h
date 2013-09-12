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
 * A predicate on tree operations.
 * First component: the absolute path corresponding to the 'path'
 * argument of setCondition(), or empty if no condition is set.
 * Second component: the file contents given as the 'value' argument of
 * setCondition().
 */
typedef std::pair<std::string, std::string> Condition;

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

    virtual std::pair<uint64_t, Configuration> getConfiguration() = 0;
    virtual ConfigurationResult setConfiguration(
                uint64_t oldId,
                const Configuration& newConfiguration) = 0;

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
    virtual Result canonicalize(const std::string& path,
                                const std::string& workingDirectory,
                                std::string& canonical) = 0;
    /// See Cluster::makeDirectory.
    virtual Result makeDirectory(const std::string& path,
                                 const std::string& workingDirectory,
                                 const Condition& condition) = 0;
    /// See Cluster::listDirectory.
    virtual Result listDirectory(const std::string& path,
                                 const std::string& workingDirectory,
                                 const Condition& condition,
                                 std::vector<std::string>& children) = 0;
    /// See Cluster::removeDirectory.
    virtual Result removeDirectory(const std::string& path,
                                   const std::string& workingDirectory,
                                   const Condition& condition) = 0;
    /// See Cluster::write.
    virtual Result write(const std::string& path,
                         const std::string& workingDirectory,
                         const std::string& contents,
                         const Condition& condition) = 0;
    /// See Cluster::read.
    virtual Result read(const std::string& path,
                        const std::string& workingDirectory,
                        const Condition& condition,
                        std::string& contents) = 0;
    /// See Cluster::removeFile.
    virtual Result removeFile(const std::string& path,
                              const std::string& workingDirectory,
                              const Condition& condition) = 0;

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
