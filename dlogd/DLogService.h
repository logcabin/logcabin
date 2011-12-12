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
 * This file declares the DLogService class.
 */

#include <memory>

#include <DLogRPC.h>

#ifndef DLOGD_DLOGSERVICE_H
#define DLOGD_DLOGSERVICE_H

namespace DLog {

/**
 * The is the DLogService class.
 */
class DLogService : public RPC::Service {
  public:
    /**
     * Constructor.
     */
    DLogService();
    virtual ~DLogService();
    /**
     * \copydoc RPC::Service::getServiceId
     */
    virtual RPC::ServiceId getServiceId() const;
    /**
     * Process a DLog service request.
     * \copydetails RPC::Service::processMessage
     */
    virtual void processMessage(RPC::Opcode op,
                                std::unique_ptr<RPC::Message> request,
                                std::unique_ptr<RPC::Message> reply);
    /**
     * Open a log.
     * \param request
     *      RPC request message.
     * \param reply
     *      A preinitialized reply message.
     */
    void openLog(std::unique_ptr<RPC::Message> request,
                 std::unique_ptr<RPC::Message> reply);
    /**
     * Delete a log.
     * \copydetails openLog
     */
    void deleteLog(std::unique_ptr<RPC::Message> request,
                   std::unique_ptr<RPC::Message> reply);
    /**
     * Append to a log.
     * \copydetails openLog
     */
    void append(std::unique_ptr<RPC::Message> request,
                std::unique_ptr<RPC::Message> reply);
    /**
     * Invalidate chunks of a log.
     * \copydetails openLog
     */
    void invalidate(std::unique_ptr<RPC::Message> request,
                    std::unique_ptr<RPC::Message> reply);
    /**
     * Read from a log.
     * \copydetails openLog
     */
    void read(std::unique_ptr<RPC::Message> request,
              std::unique_ptr<RPC::Message> reply);
    /**
     * Get the head of a log.
     * \copydetails openLog
     */
    void getHead(std::unique_ptr<RPC::Message> request,
                 std::unique_ptr<RPC::Message> reply);
  private:
};

} // namespace

#endif /* DLOGD_DLOGSERVICE_H */
