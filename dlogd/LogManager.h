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

#include "AsyncMutex.h"
#include "DLogStorage.h"
#include "Ref.h"
#include "build/dlogd/InternalLog.pb.h"

#ifndef DLOGD_LOGMANAGER_H
#define DLOGD_LOGMANAGER_H

namespace DLog {

/**
 * The main class for managing and accessing logs.
 */
class LogManager {
  private:
    /**
     * Log 0 is reserved for describing the remaining logs.
     */
    enum { INTERNAL_LOG_ID = 0 };

  public:
    /// See createLog().
    class CreateCallback : public BaseCallback {
      public:
        virtual void created(LogId logId) = 0;
        friend class RefHelper<CreateCallback>;
        friend class MakeHelper;
    };

    /// See deleteLog().
    class DeleteCallback : public BaseCallback {
      public:
        virtual void deleted() = 0;
        friend class RefHelper<DeleteCallback>;
        friend class MakeHelper;
    };

    /// See Constructor.
    class InitializeCallback : public BaseCallback {
      public:
        virtual void initialized() = 0;
        friend class RefHelper<InitializeCallback>;
        friend class MakeHelper;
    };

  private:

    /// Called by replay methods when they're done.
    class ReplayCallback : public BaseCallback {
      public:
        virtual void replayed() = 0;
        friend class RefHelper<ReplayCallback>;
        friend class MakeHelper;
    };

    /**
     * Holds metadata for logs.
     * Use logNames can look these up by name, and use logs to look these up by
     * log ID.
     */
    struct LogInfo {
        explicit LogInfo(const std::string& logName);
        RefHelper<LogInfo>::RefCount refCount;
        /**
         * The log's name.
         */
        const std::string logName;
        /**
         * The log's ID. Before the log is fully created or after it's been
         * deleted, this will be set to NO_LOG_ID.
         */
        LogId logId;
        /**
         * If the log has been deleted or has not been fully created,
         * this will be NULL.
         */
        Ptr<Storage::Log> log;
        /**
         * Protects from concurrent creates and deletes to the log by this
         * name.
         */
        AsyncMutex mutex;
        LogInfo(const LogInfo& other) = delete;
        LogInfo& operator=(const LogInfo& other) = delete;
    };


    /**
     * Constructor.
     * \param storageModule
     *      Used to store logs durably.
     * \param initializeCompletion
     *      Until this fires, the caller is not allowed to access this class.
     */
    explicit LogManager(Ref<Storage::StorageModule> storageModule,
                        Ref<InitializeCallback> initializeCompletion);

  public:

    /**
     * Create or open a log by name.
     * \param logName
     *      The name of the log to create. If a log by this name has already
     *      been created, this will immediately call the completion.
     * \param createCompletion
     *      Called once the log exists.
     */
    void createLog(const std::string& logName,
                   Ref<CreateCallback> createCompletion);

    /**
     * Delete the log by the given name.
     * \param logName
     *      The name of the log. If no log by this name exists, this will
     *      immediately call the completion.
     * \param deleteCompletion
     *      Called once the log has been deleted.
     */
    void deleteLog(const std::string& logName,
                   Ref<DeleteCallback> deleteCompletion);

    /**
     * Return a handle to the log with the given ID.
     * \param logId
     *      The identifier for the log. (If you pass NO_LOG_ID here, you'll get
     *      NULL out the other end.) You may not get internal logs this way.
     * \return
     *      The Log, if it was found; otherwise, return NULL.
     */
    Ptr<Storage::Log> getLog(LogId logId);

    /**
     * Return the list of logs.
     * \return
     *      The list of logs in alphabetical order.
     *      This does not include the names of logs for internal use.
     */
    std::vector<std::string> listLogs();

  private:
    /**
     * Create the metadata log entry in the internal log.
     * \param initializeCompletion
     *      This is fired once the metadata log entry is durable.
     */
    void initializeStorage(Ref<InitializeCallback> initializeCompletion);
    /**
     * Replay an internal log entry.
     */
    void replayLogEntry(const Storage::LogEntry& entry,
                        Ref<ReplayCallback> completion);
    /// Replay an invalidation in the internal log.
    void replayLogInvalidation(EntryId entryId,
                               Ref<ReplayCallback> completion);
    /// Replay the Metadata entry.
    void replayMetadataEntry(const Storage::LogEntry& entry,
              const ProtoBuf::InternalLog::Metadata& metadata,
              Ref<ReplayCallback> completion);
    /// Replay a DeclareLog entry.
    void replayDeclareLogEntry(const Storage::LogEntry& entry,
              const ProtoBuf::InternalLog::DeclareLog& declareLog,
              Ref<ReplayCallback> completion);

    /// Reference count.
    RefHelper<LogManager>::RefCount refCount;
    /// Set to true once the Constructor and initializeStorage are done.
    bool initialized;
    /// The module providing the underlying durable storage.
    Ref<Storage::StorageModule> storageModule;
    /// A handle to the internal log.
    Ref<Storage::Log> internalLog;
    /**
     * The logs on storage that have been fully created.
     * Does not include the internal log.
     */
    std::unordered_map<LogId, Ref<LogInfo>> logs;
    /**
     * Contains the names of all logs that partially or fully exist.
     */
    std::unordered_map<std::string, Ref<LogInfo>> logNames;
    /**
     * A globally unique ID for the storage.
     */
    const std::string uuid;

    // Internal helper classes.
    class NoOpStorageDeleteCallback;
    class ConstructorReplayCallback;
    class InitializeAppendCallback;
    class CreateLogMutexCallback;
    class CreateLogAppendCallback;
    class CreateLogReplayCallback;
    class DeleteLogMutexCallback;
    class DeleteLogInvalidateCallback;
    class DeleteLogReplayCallback;
    class LogDestroyedCallback;

    friend class MakeHelper;
    friend class RefHelper<LogManager>;
};

} // namespace DLog

#endif /* DLOGD_LOGMANAGER_H */
