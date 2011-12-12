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
 * Contains the LogManager class.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Ref.h"
#include "DLogStorage.h"

#ifndef DLOGD_LOGMANAGER_H
#define DLOGD_LOGMANAGER_H

namespace DLog {

/**
 * The main class for managing and accessing logs.
 */
class LogManager {
  public:
    /**
     * See createLog().
     */
    class CreateCallback {
      public:
        virtual ~CreateCallback() {}
        virtual void created(LogId logId) = 0;
    };

    /**
     * See deleteLog().
     */
    class DeleteCallback {
      public:
        virtual ~DeleteCallback() {}
        virtual void deleted() = 0;
    };

    /**
     * Constructor.
     * \param storageModule
     *      Used to store logs durably.
     */
    explicit LogManager(std::unique_ptr<Storage::StorageModule> storageModule);

    /**
     * Create or open a log by name.
     * \param logName
     *      The name of the log to create. If a log by this name has already
     *      been created, this will immediately call the completion.
     * \param createCompletion
     *      Called once the log has been created.
     */
    void createLog(const std::string& logName,
                   std::unique_ptr<CreateCallback> createCompletion);

    /**
     * Delete the log by the given name.
     * \param logName
     *      The name of the log. If no log by this name exists, this will
     *      immediately call the completion.
     * \param deleteCompletion
     *      Called once the log has been deleted.
     */
    void deleteLog(const std::string& logName,
                   std::unique_ptr<DeleteCallback> deleteCompletion);

    /**
     * Return a handle to the log with the given ID.
     * \param logId
     *      The identifier for the log. (If you pass NO_LOG_ID here, you'll get
     *      NULL out the other end.)
     * \return
     *      The Log, if it was found; otherwise, return NULL.
     */
    Ptr<Storage::Log> getLog(LogId logId);

    /**
     * Return the list of logs.
     */
    std::vector<std::string> listLogs();

  private:
    std::unique_ptr<Storage::StorageModule> storageModule;
    std::unordered_map<std::string, LogId> logNames;
    std::unordered_map<LogId, Ref<Storage::Log>> logs;
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;
};

} // namespace DLog

#endif /* DLOGD_LOGMANAGER_H */
