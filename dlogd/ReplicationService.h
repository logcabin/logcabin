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
 * This file declares the ReplicationService class.
 */

#include <memory>

#include <DLogRPC.h>

#ifndef DLOGD_REPLICATIONSERVICE_H
#define DLOGD_REPLICATIONSERVICE_H

namespace DLog {

/**
 * The is the DLogService class.
 */
class ReplicationService : public RPC::Service {
  public:
    /**
     * Constructor.
     */
    ReplicationService();
    virtual ~ReplicationService();
    virtual RPC::ServiceId getServiceId() const;
    virtual void processMessage(RPC::Opcode op,
                                std::unique_ptr<RPC::Message> request,
                                std::unique_ptr<RPC::Message> reply);
    /**
     * Process heartbeat.
     * \param request
     *      RPC request message.
     * \param reply
     *      A preinitialized reply message.
     */
    void heartbeat(std::unique_ptr<RPC::Message> request,
                   std::unique_ptr<RPC::Message> reply);
  private:
};

} // namespace

#endif /* DLOGD_REPLICATIONSERVICE_H */
