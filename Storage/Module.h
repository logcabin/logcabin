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

#include <vector>

#include "Storage/Types.h"

#ifndef LOGCABIN_STORAGE_MODULE_H
#define LOGCABIN_STORAGE_MODULE_H

namespace LogCabin {
namespace Storage {

// forward declaration
class Log;

/**
 * An interface for managing logs on durable storage.
 * This is a bit of a dangerous interface which is only used by
 * Server::LogManager.
 */
class Module {
  public:
    /// Constructor.
    Module() {}

    /// Destructor.
    virtual ~Module() {}

    /**
     * Scan the logs on the durable storage and return their IDs.
     * \return
     *      The ID of each log in the system, in no specified order.
     */
    virtual std::vector<LogId> getLogs() = 0;

    /**
     * Open a log.
     * This will create or finish creating the log if it does not fully exist.
     *
     * Creating a log need not be atomic but must be idempotent. After even a
     * partial create, getLogs() must return this log and deleteLog() must be
     * able to delete any storage resources allocated to this log.
     *
     * \param logId
     *      A log ID. The caller must make sure not to create multiple Log
     *      objects for the same log ID.
     * \return
     *      A new Log object that the caller must 'delete'.
     */
    virtual Log* openLog(LogId logId) = 0;

    /**
     * Delete a log from stable storage.
     *
     * This need not be atomic but must be idempotent. After a successful
     * delete, getLogs() may not return this log again. If the delete fails,
     * however, getLogs() must continue to return this log.
     *
     * \param logId
     *      The caller asserts that no Log object exists for this ID.
     */
    virtual void deleteLog(LogId logId) = 0;

    // Module is not copyable.
    Module(const Module& other) = delete;
    Module& operator=(const Module&& other) = delete;
};

} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_MODULE_H */
