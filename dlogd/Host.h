/* Copyright (c) 2011 Stanford University
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

/**
 * \file
 * This file declares the interface for the Host class.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <Ref.h>
#include <DLogRPC.h>

#ifndef HOST_H
#define HOST_H

namespace DLog {

/**
 * The class that manages the state of an individual cluster member.
 */
class Host {
    public:
    enum HostStatus {
        /// Host is alive an healthy.
        HOSTSTATUS_UP,
        /// Host missed one or more heartbeats.
        HOSTSTATUS_MISSHB,
        /// Host is not responding.
        HOSTSTATUS_DOWN,
    };
    /**
     * Constructor.
     * \param host
     *      Host ip and port.
     */
    Host(RPC::Transport& t, const std::string& host);
    ~Host();
    /**
     * Attempt to establish a connection to the host.
     */
    void connect();
    /**
     * Get the bulk data RPC session object for this host.
     * \return
     *      A RPC::Session.
     */
    RPC::Session& getDataSession();
    /**
     * Get the control RPC session object for this host.
     * \return
     *      A RPC::Session.
     */
    RPC::Session& getControlSession();
    /**
     * Get the hostname of this cluster member.
     * \return
     *      The hostname.
     */
    std::string getHostname();
    /**
     * Get the host status.
     * \return
     *      The host status.
     */
    HostStatus getStatus();
    /**
     * Get the number of lost heartbeats.
     * \return
     *      Number of heartbeats lost since the machine joined the cluster.
     */
    uint32_t getLostHeartbeats();
private:
    /// Transport object
    RPC::Transport* transport;
    /// Data session object
    RPC::Session* dataSession;
    /// Control session object
    RPC::Session* controlSession;
    uint32_t refCount;
    friend class RefHelper<Host>;
};

} // namespace

#endif /* HOST_H */
