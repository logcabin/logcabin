/* Copyright (c) 2011-2012 Stanford University
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
 * This file declares the interface for DLog's RPC library.
 */

#include "DLogEvent.h"
#include "DLogRPC.h"

#ifndef DLOGECHOSERVICE_H
#define DLOGECHOSERVICE_H

namespace DLog {
namespace RPC {

/**
 * RPC service interface that all services derive from.
 */
class EchoService : public Service {
  public:
    /**
     * Constructor.
     */
    EchoService();
    virtual ~EchoService();
    /**
     * Return the service id of this service.
     * \return
     *      Service id.
     */
    virtual ServiceId getServiceId() const;
    /**
     * Process an incoming service request.
     * \param server
     *      RPC server instance.
     * \param op
     *      RPC opcode
     * \param request
     *      RPC request message
     */
    virtual void processMessage(Server& server,
                                Opcode op,
                                Message& request);
  private:
    EchoService(const EchoService&) = delete;
    EchoService& operator=(const EchoService&) = delete;
};

} // namespace
} // namespace

#endif /* DLOGECHOSERVICE_H */
