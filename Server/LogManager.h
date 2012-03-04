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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/RWPtr.h"
#include "Storage/Module.h"
#include "Storage/Types.h"

#ifndef LOGCABIN_SERVER_LOGMANAGER_H
#define LOGCABIN_SERVER_LOGMANAGER_H

namespace LogCabin {

// forward declarations
namespace Core {
class Config;
}
namespace ProtoBuf {
namespace InternalLog {
class Metadata;
class DeclareLog;
}
}
namespace Storage {
class LogEntry;
class Log;
}

namespace Server {

/**
 * The main class for managing and accessing logs.
 * This class is intended to be accessed via an RWManager, which provides a
 * readers-writer lock.
 */
class LogManager {
  public:
    typedef Storage::LogId LogId;

  public:

    /**
     * Constructor.
     * \param config
     *      Configuration options.
     * \param storageModuleForTesting
     *      During testing, it can be convenient to pass in an existing storage
     *      module for the log manager to use.
     */
    LogManager(const Core::Config& config,
               std::unique_ptr<Storage::Module> storageModuleForTesting =
                    std::unique_ptr<Storage::Module>());

    /**
     * Destructor.
     */
    ~LogManager();

    /**
     * Create or open a log by name.
     * This must be called with exclusive access to this object.
     * \param logName
     *      The name of the log to create. If a log by this name has already
     *      been created, this will immediately return.
     */
    LogId createLog(const std::string& logName);

    /**
     * Delete the log by the given name.
     * This must be called with exclusive access to this object.
     * \param logName
     *      The name of the log. If no log by this name exists, this will
     *      immediately return.
     */
    void deleteLog(const std::string& logName);

    /**
     * Return a read-only handle to the log with the given ID.
     * \param logId
     *      The identifier for the log. (If you pass NO_LOG_ID here, you'll get
     *      NULL out the other end.) You may not get internal logs this way.
     * \return
     *      The Log, if it was found; otherwise, return NULL.
     */
    Core::RWPtr<const Storage::Log> getLogShared(LogId logId) const;

    /**
     * Return an exclusive, writable handle to the log with the given ID.
     * \param logId
     *      The identifier for the log. (If you pass NO_LOG_ID here, you'll get
     *      NULL out the other end.) You may not get internal logs this way.
     * \return
     *      The Log, if it was found; otherwise, return NULL.
     */
    Core::RWPtr<Storage::Log> getLogExclusive(LogId logId) const;

    /**
     * Return the list of logs.
     * \return
     *      The list of logs in alphabetical order.
     *      This does not include the names of logs for internal use.
     */
    std::vector<std::string> listLogs() const;

  private:
    /**
     * Log 0 is reserved for describing the remaining logs.
     */
    enum { INTERNAL_LOG_ID = 0 };

    /**
     * Holds metadata for logs. Use #logNames can look these up by name, and
     * use #logs to look these up by log ID.
     */
    struct LogInfo {
        explicit LogInfo(const std::string& logName);
        /**
         * The log's name.
         */
        const std::string logName;
        /**
         * The log's ID.
         */
        LogId logId;
        /**
         * The log itself. This is managed by an RWManager, which contains a
         * readers-writer lock.
         */
        Core::RWManager<Storage::Log> log;

        // LogInfo is not copyable
        LogInfo(const LogInfo& other) = delete;
        LogInfo& operator=(const LogInfo& other) = delete;
    };

    /**
     * Create the metadata log entry in the internal log.
     */
    void initializeStorage();

    /**
     * Calls replayLogEntry() for every log entry in the internal log that
     * hasn't already been replayed.
     */
    void replayLogEntries();

    /// Replay an internal log entry.
    void replayLogEntry(const Storage::LogEntry& entry);

    /// Replay an invalidation in the internal log.
    void replayLogInvalidation(Storage::EntryId entryId);

    /// Replay the Metadata entry.
    void replayMetadataEntry(const Storage::LogEntry& entry,
              const ProtoBuf::InternalLog::Metadata& metadata);

    /// Replay a DeclareLog entry.
    void replayDeclareLogEntry(const Storage::LogEntry& entry,
              const ProtoBuf::InternalLog::DeclareLog& declareLog);

    /**
     * A globally unique ID for the storage.
     */
    const std::string uuid;

    /// The module providing the underlying durable storage.
    std::unique_ptr<Storage::Module> storageModule;

    /// A handle to the internal log.
    std::unique_ptr<Storage::Log> internalLog;

    /**
     * This is the smallest EntryId that has not been replayed by
     * replayLogEntry(). It is used to keep track of where replayLogEntries()
     * left off.
     */
    uint64_t nextInternalLogEntry;

    /**
     * The logs on storage that have been fully created.
     * Does not include the internal log.
     */
    std::unordered_map<LogId, std::shared_ptr<LogInfo>> logs;

    /**
     * Contains the names of all logs that partially or fully exist.
     */
    std::unordered_map<std::string, std::shared_ptr<LogInfo>> logNames;

    // LogManager is not copyable
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_LOGMANAGER_H */
