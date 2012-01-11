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

#include <algorithm>
#include <set>

#include "Common.h"
#include "Config.h"
#include "Debug.h"
#include "LogManager.h"

namespace DLog {

using namespace Storage; // NOLINT

LogManager::LogInfo::LogInfo(const std::string& logName)
    : refCount()
    , logName(logName)
    , logId(NO_LOG_ID)
    , log()
    , mutex()
{
}

/**
 * No one waits on calls to delete logs on storage, since if they fail, they'll
 * be restarted when the daemon starts up again.
 */
class LogManager::NoOpStorageDeleteCallback
                        : public StorageModule::DeleteCallback {
  private:
    NoOpStorageDeleteCallback() = default;
  public:
    void deleted(LogId logId) {}
    friend class RefHelper<NoOpStorageDeleteCallback>;
    friend class MakeHelper;
};


LogManager::LogManager(const Config& config,
                       Ref<StorageModule> storageModule,
                       Ref<InitializeCallback> initializeCompletion)
    : refCount()
    , initialized(false)
    , storageModule(storageModule)
    , internalLog()
    , logs()
    , logNames()
    , uuid(config.read<std::string>("uuid"))
{
    if (uuid.length() < 10) {
        PANIC("This is a poor choice of a UUID (%s). Refusing to proceed.",
              uuid.c_str());
    }
    LOG(NOTICE, "Initializing log manager with UUID %s", uuid.c_str());
    storageModule->openLog(INTERNAL_LOG_ID,
           make<ConstructorInternalLogOpenedCallback>(
                Ref<LogManager>(*this),
                initializeCompletion));
}

/**
 * This continues the constructor once the internal log has been opened.
 */
class LogManager::ConstructorInternalLogOpenedCallback
                    : public StorageModule::OpenCallback {
    ConstructorInternalLogOpenedCallback(
                            Ref<LogManager> logManager,
                            Ref<InitializeCallback> initializeCompletion)
        : logManager(logManager)
        , initializeCompletion(initializeCompletion)
    {
    }
  public:
    void opened(Ref<Log> log) {
        logManager->internalLog = log;

        std::vector<LogId> foundLogs = logManager->storageModule->getLogs();
        std::deque<LogEntry> internalLogEntries =
                        logManager->internalLog->readFrom(0);

        // Read from the internal log.
        if (internalLogEntries.empty()) {
            if (!hasOnly(foundLogs, LogId(INTERNAL_LOG_ID))) {
                PANIC("It looks like there's some left-over data stored here, "
                      "but all the metadata is gone?");
            }
            // There's nothing at all here; create the storage.
            logManager->initializeStorage(initializeCompletion);
            return;
        }

        // Replay the internal log.
        make<ConstructorReplayCallback>(logManager,
                                        foundLogs, internalLogEntries,
                                        initializeCompletion);
    }
  private:
    Ref<LogManager> logManager;
    Ref<InitializeCallback> initializeCompletion;
    friend class RefHelper<ConstructorInternalLogOpenedCallback>;
    friend class MakeHelper;
};


/**
 * This is used in the constructor to replay the internal log.
 */
class LogManager::ConstructorReplayCallback : public ReplayCallback {
    ConstructorReplayCallback(Ref<LogManager> logManager,
                              std::vector<LogId> foundLogs,
                              std::deque<LogEntry> internalLogEntries,
                              Ref<InitializeCallback> initializeCompletion)
        : logManager(logManager)
        , foundLogs(foundLogs)
        , internalLogEntries(internalLogEntries)
        , initializeCompletion(initializeCompletion)
    {
        next();
    }
  public:
    void replayed() {
        if (!internalLogEntries.empty()) {
            next();
            return;
        }

        // Delete any logs that shouldn't exist.
        for (auto it = foundLogs.begin(); it != foundLogs.end(); ++it) {
            LogId logId = *it;
            if (logId == INTERNAL_LOG_ID)
                continue;
            if (logManager->logs.find(logId) != logManager->logs.end())
                continue;
            // Don't wait for the delete to finish,
            // since it's just cleaning up unreachable storage.
            logManager->storageModule->deleteLog(logId,
                                         make<NoOpStorageDeleteCallback>());
        }
        logManager->initialized = true;
        initializeCompletion->initialized();
    }
  private:
    void next() {
        assert(!internalLogEntries.empty());
        LogEntry entry = internalLogEntries.front();
        internalLogEntries.pop_front();
        logManager->replayLogEntry(entry, Ref<ReplayCallback>(*this));
    }
    Ref<LogManager> logManager;
    const std::vector<LogId> foundLogs;
    std::deque<LogEntry> internalLogEntries;
    Ref<InitializeCallback> initializeCompletion;
    friend class RefHelper<ConstructorReplayCallback>;
    friend class MakeHelper;
};

void
LogManager::initializeStorage(Ref<InitializeCallback> initializeCompletion)
{
    LOG(NOTICE, "Initializing your internal log with UUID %s.",
                uuid.c_str());
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid(uuid);
    LogEntry entry(/* log id = */ LogManager::INTERNAL_LOG_ID,
                   /* entry id = */ 0,
                   /* TODO(ongaro): createTime = */ 0,
                   /* data = */ Chunk::makeChunk(contents));
    assert(internalLog->getLastId() == NO_ENTRY_ID);
    internalLog->append(entry,
                        make<InitializeAppendCallback>(Ref<LogManager>(*this),
                                                       initializeCompletion));
}

/**
 * This is used in initializeStorage().
 * It fires once the internal log entry with metadata has been written,
 * and it in turn fires the user's initializeCompletion.
 */
class LogManager::InitializeAppendCallback : public Log::AppendCallback {
  private:
    InitializeAppendCallback(Ref<LogManager> logManager,
                             Ref<InitializeCallback> initializeCompletion)
        : logManager(logManager)
        , initializeCompletion(initializeCompletion)
    {
    }
  public:
    void appended(LogEntry entry) {
        assert(entry.entryId == 0);
        logManager->initialized = true;
        initializeCompletion->initialized();
    }
    Ref<LogManager> logManager;
    Ref<LogManager::InitializeCallback> initializeCompletion;
    friend class RefHelper<InitializeAppendCallback>;
    friend class MakeHelper;
};

void
LogManager::createLog(const std::string& logName,
                      Ref<CreateCallback> createCompletion)
{
    LOG(DBG, "createLog(%s)", logName.c_str());
    assert(initialized);

    auto it = logNames.find(logName);
    if (it == logNames.end()) {
        it = logNames.insert({logName, make<LogInfo>(logName)}).first;
    }
    Ref<LogInfo> logInfo = it->second;
    logInfo->mutex.acquire(
        make<CreateLogMutexCallback>(Ref<LogManager>(*this),
                                     logInfo,
                                     createCompletion));
}

class LogManager::CreateLogMutexCallback : public AsyncMutex::Callback {
    CreateLogMutexCallback(Ref<LogManager> logManager,
                           Ref<LogInfo> logInfo,
                           Ref<CreateCallback> createCompletion)
        : logManager(logManager)
        , logInfo(logInfo)
        , createCompletion(createCompletion) {
    }
  public:
    void acquired() {
        // Has the log already been created?
        if (logInfo->log) {
            createCompletion->created(logInfo->logId);
            logInfo->mutex.release();
            return;
        }

        // Append to the internal log.
        // The actual storage create will begin once the internal log has been
        // safely written.
        ProtoBuf::InternalLog::LogEntry contents;
        contents.set_type(contents.DECLARE_LOG_TYPE);
        ProtoBuf::InternalLog::DeclareLog& declareLog =
            *contents.mutable_declare_log();
        declareLog.set_log_name(logInfo->logName);
        LogEntry entry(/* logId = */ NO_LOG_ID,
                       /* entryId = */ NO_ENTRY_ID,
                       /* createTime = */ 0,
                       /* data = */ Chunk::makeChunk(contents));
        logManager->internalLog->append(entry,
                make<CreateLogAppendCallback>(logManager,
                                              logInfo,
                                              createCompletion));
    }
  private:
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<CreateCallback> createCompletion;
    friend class RefHelper<CreateLogMutexCallback>;
    friend class MakeHelper;
};

/// Helper for createLog().
class LogManager::CreateLogAppendCallback : public Log::AppendCallback {
    CreateLogAppendCallback(Ref<LogManager> logManager,
                            Ref<LogInfo> logInfo,
                            Ref<CreateCallback> createCompletion)
        : logManager(logManager)
        , logInfo(logInfo)
        , createCompletion(createCompletion) {
    }
  public:
    void appended(LogEntry entry) {
        LogId logId = entry.entryId;
        logInfo->logId = logId;
        logManager->replayLogEntry(entry,
                   make<CreateLogReplayCallback>(logManager,
                                                 logInfo,
                                                 createCompletion));
    }
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<CreateCallback> createCompletion;
    friend class RefHelper<CreateLogAppendCallback>;
    friend class MakeHelper;
};

/// Helper for createLog().
class LogManager::CreateLogReplayCallback : public ReplayCallback {
    CreateLogReplayCallback(Ref<LogManager> logManager,
                            Ref<LogInfo> logInfo,
                            Ref<CreateCallback> createCompletion)
        : logManager(logManager)
        , logInfo(logInfo)
        , createCompletion(createCompletion) {
    }
  public:
    void replayed() {
        createCompletion->created(logInfo->logId);
        logInfo->mutex.release();
    }
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<CreateCallback> createCompletion;
    friend class RefHelper<CreateLogReplayCallback>;
    friend class MakeHelper;
};

void
LogManager::deleteLog(const std::string& logName,
                      Ref<DeleteCallback> deleteCompletion)
{
    LOG(DBG, "deleteLog(%s)", logName.c_str());
    assert(initialized);

    // If the log is not in logNames, it has already been deleted.
    Ptr<LogInfo> tmpLogInfo;
    {
        auto it = logNames.find(logName);
        if (it == logNames.end()) {
            deleteCompletion->deleted();
            return;
        }
        tmpLogInfo = it->second;
    }
    Ref<LogInfo> logInfo(*tmpLogInfo);

    logInfo->mutex.acquire(
        make<DeleteLogMutexCallback>(Ref<LogManager>(*this),
                                     logInfo,
                                     deleteCompletion));
}

class LogManager::DeleteLogMutexCallback : public AsyncMutex::Callback {
    DeleteLogMutexCallback(Ref<LogManager> logManager,
                           Ref<LogInfo> logInfo,
                           Ref<DeleteCallback> deleteCompletion)
        : logManager(logManager)
        , logInfo(logInfo)
        , deleteCompletion(deleteCompletion) {
    }
  public:
    void acquired() {
        // Has the log already been deleted? Note: the logInfo->mutex
        // guarantees no partial deletes are in progress.
        if (!logInfo->log) {
            deleteCompletion->deleted();
            logInfo->mutex.release();
            return;
        }

        // Invalidate the internal log entry.
        // The actual storage delete will begin once the internal log has been
        // safely written.
        LogEntry entry(/* log id = */ INTERNAL_LOG_ID,
                       /* entry id = */ NO_ENTRY_ID,
                       /* TODO(ongaro): createTime = */ 0,
                       /* invalidations = */ { logInfo->logId });
        logManager->internalLog->append(entry,
                make<DeleteLogInvalidateCallback>(logManager,
                                                  logInfo,
                                                  deleteCompletion));
    }
  private:
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<DeleteCallback> deleteCompletion;
    friend class RefHelper<DeleteLogMutexCallback>;
    friend class MakeHelper;
};

/// Helper for log deletion.
class LogManager::DeleteLogInvalidateCallback : public Log::AppendCallback {
  private:
    DeleteLogInvalidateCallback(Ref<LogManager> logManager,
                                Ref<LogInfo> logInfo,
                                Ref<DeleteCallback> deleteCompletion)
        : logManager(logManager)
        , logInfo(logInfo)
        , deleteCompletion(deleteCompletion) {
    }
  public:
    virtual void appended(LogEntry entry) {
        logManager->replayLogEntry(entry,
                make<DeleteLogReplayCallback>(logManager,
                                              logInfo,
                                              deleteCompletion));
    }
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<DeleteCallback> deleteCompletion;
    friend class RefHelper<DeleteLogInvalidateCallback>;
    friend class DLog::MakeHelper;
};

/// Helper for log deletion.
class LogManager::DeleteLogReplayCallback : public ReplayCallback {
  private:
    DeleteLogReplayCallback(Ref<LogManager> logManager,
                            Ref<LogInfo> logInfo,
                            Ref<DeleteCallback> deleteCompletion)
        : logManager(logManager)
        , logInfo(logInfo)
        , deleteCompletion(deleteCompletion) {
    }
  public:
    virtual void replayed() {
        deleteCompletion->deleted();
        logInfo->mutex.release();
    }
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<DeleteCallback> deleteCompletion;
    friend class RefHelper<DeleteLogReplayCallback>;
    friend class DLog::MakeHelper;
};

Ptr<Log>
LogManager::getLog(LogId logId)
{
    assert(initialized);
    auto it = logs.find(logId);
    if (it == logs.end())
        return Ptr<Log>(NULL);
    Ref<LogInfo> logInfo = it->second;
    return logInfo->log;
}

std::vector<std::string>
LogManager::listLogs()
{
    assert(initialized);
    return sorted(getKeys(logNames));
}

void
LogManager::replayLogEntry(const LogEntry& entry,
                           Ref<ReplayCallback> completion)
{
    const char* MALFORMED_MSG =
        "This probably means you're running an older version of "
        "LogCabin against a log generated by a newer version.";

    if (!((entry.invalidations.size() == 1 && entry.data == NO_DATA) ||
          (entry.invalidations.size() == 0 && entry.data != NO_DATA))) {
        PANIC("Internal log entry should have one invalidation xor some data."
              " %s", MALFORMED_MSG);
    }
    if (!entry.invalidations.empty()) {
        replayLogInvalidation(entry.invalidations.front(), completion);
        return;
    }

    ProtoBuf::InternalLog::LogEntry contents;
    bool parsed = contents.ParseFromArray(entry.data->getData(),
                                          entry.data->getLength());
    if (!parsed) {
        PANIC("Failed to parse protocol buffer "
              "in entry %lu of internal log. %s",
              entry.entryId, MALFORMED_MSG);
    }

    if (entry.entryId == 0 && contents.type() != contents.METADATA_TYPE) {
        PANIC("EntryId 0 should be a Metadata entry, not %d",
              contents.type());
    } else if (entry.entryId != 0 &&
               contents.type() == contents.METADATA_TYPE) {
        PANIC("Metadata entry should only appear at entryId 0, not %lu",
              entry.entryId);
    }

    switch (contents.type()) {
        case ProtoBuf::InternalLog::LogEntry::METADATA_TYPE:
            if (contents.has_metadata()) {
                replayMetadataEntry(entry, contents.metadata(), completion);
                return;
            }
            break;
        case ProtoBuf::InternalLog::LogEntry::DECLARE_LOG_TYPE:
            if (contents.has_declare_log()) {
                replayDeclareLogEntry(entry, contents.declare_log(),
                                      completion);
                return;
            }
            break;
        default:
            PANIC("Unknown entry type (%d) in entry %lu of internal log. %s",
                  contents.type(), entry.entryId, MALFORMED_MSG);
    }

    PANIC("Entry type (%d) in entry %lu of internal log "
          "does not match its contents. This is probably a nasty bug.",
          contents.type(), entry.entryId);
}

void
LogManager::replayLogInvalidation(EntryId entryId,
                                  Ref<ReplayCallback> completion)
{
    // This is probably invalidating a DeclareLog entry -- deleting a log.
    LogId logId = entryId;
    LOG(DBG, "Deleting %lu", logId);

    auto it = logs.find(logId);
    if (it == logs.end()) {
        // This log doesn't actually exist.
        completion->replayed();
        return;
    }

    Ref<LogInfo> logInfo = it->second;
    // This is slightly dubious, but should work in the current system, since
    // no other deletes and creates can be happening concurrently.
    Ref<Log> log(*logInfo->log);
    logInfo->log.reset();
    // Wait for the Log to be destroyed.
    log->addDestructorCallback(
            make<LogDestroyedCallback>(Ref<LogManager>(*this),
                                       logInfo,
                                       completion));
}

/// Helper for log deletion.
class LogManager::LogDestroyedCallback : public Log::DestructorCallback {
  private:
    LogDestroyedCallback(Ref<LogManager> logManager,
                         Ref<LogInfo> logInfo,
                         Ref<ReplayCallback> completion)
        : logManager(logManager)
        , logInfo(logInfo)
        , completion(completion) {
    }
  public:
    void destructorCallback(LogId logId) {
        LOG(DBG, "Log destroyed");
        logManager->logNames.erase(logInfo->logName);
        logManager->logs.erase(logInfo->logId);
        logInfo->logId = NO_LOG_ID;

        // The log is now fully deleted as far as anyone can tell. The only
        // thing left is to reclaim its storage resources.
        completion->replayed();

        // Start deleting the underlying storage for the Log.
        // If this process crashes, the storage delete will be restarted during
        // initialization when the process comes back up.
        logManager->storageModule->deleteLog(logId,
                       make<NoOpStorageDeleteCallback>());
    }
    Ref<LogManager> logManager;
    Ref<LogInfo> logInfo;
    Ref<ReplayCallback> completion;
    friend class RefHelper<LogDestroyedCallback>;
    friend class MakeHelper;
};


void
LogManager::replayMetadataEntry(const LogEntry& entry,
              const ProtoBuf::InternalLog::Metadata& metadata,
              Ref<ReplayCallback> completion)
{
    LOG(NOTICE, "Read UUID %s in metadata entry of internal log",
                uuid.c_str());
    if (metadata.uuid() != this->uuid) {
        PANIC("UUID found in log (%s) does not match the UUID "
              "from the config file (%s)",
              metadata.uuid().c_str(), this->uuid.c_str());
    }
    completion->replayed();
}


void
LogManager::replayDeclareLogEntry(const LogEntry& entry,
              const ProtoBuf::InternalLog::DeclareLog& declareLog,
              Ref<ReplayCallback> completion)
{
    LogId logId = entry.entryId;
    const std::string& logName = declareLog.log_name();

    // Find the log's LogInfo by its name, or create a new LogInfo if none
    // exists.
    Ptr<LogInfo> tmpLogInfo;
    {
        auto it = logNames.find(logName);
        if (it != logNames.end()) {
            tmpLogInfo = it->second;
        } else {
            Ref<LogInfo> logInfo = make<LogInfo>(logName);
            logNames.insert({logName, logInfo});
            tmpLogInfo = logInfo;
        }
    }
    Ref<LogInfo> logInfo = Ref<LogInfo>(*tmpLogInfo);
    logInfo->logId = logId;

    // Make sure that the entry in 'logs' is set.
    if (logs.find(logId) == logs.end())
        logs.insert({logId, logInfo});

    // Create the log on storage.
    if (logInfo->log) {
        WARN("Attempted to create an open log (id %lu, name '%s') -- ignored",
             logId, logName.c_str());
        completion->replayed();
    } else {
        storageModule->openLog(logId,
                               make<ReplayDeclareLogCreatedCallback>(
                                    logInfo,
                                    completion));
    }
}

/// Helper for replayDeclareLogEntry().
class LogManager::ReplayDeclareLogCreatedCallback
                        : public StorageModule::OpenCallback {
  private:
    ReplayDeclareLogCreatedCallback(Ref<LogInfo> logInfo,
                                    Ref<ReplayCallback> completion)
        : logInfo(logInfo)
        , completion(completion) {
    }
  public:
    void opened(Ref<Log> log) {
        logInfo->log = log;
        completion->replayed();
    }
    Ref<LogInfo> logInfo;
    Ref<ReplayCallback> completion;
    friend class RefHelper<ReplayDeclareLogCreatedCallback>;
    friend class MakeHelper;
};


} // namespace DLog
