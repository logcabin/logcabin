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

#include <deque>

#include "build/Server/InternalLog.pb.h"
#include "include/Common.h"
#include "include/Debug.h"
#include "Core/Config.h"
#include "Core/ProtoBuf.h"
#include "RPC/ProtoBuf.h"
#include "Server/LogManager.h"
#include "Storage/Factory.h"
#include "Storage/Log.h"
#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Server {

using Storage::LogEntry;

////////// LogManager::LogInfo //////////

LogManager::LogInfo::LogInfo(const std::string& logName)
    : logName(logName)
    , logId(Storage::NO_LOG_ID)
    , log()
{
}

////////// LogManager //////////

LogManager::LogManager(
        const Core::Config& config,
        std::unique_ptr<Storage::Module> storageModuleForTesting)
    : uuid(config.read<std::string>("uuid"))
    , storageModule(std::move(storageModuleForTesting))
    , internalLog()
    , nextInternalLogEntry(0)
    , logs()
    , logNames()
{
    LOG(NOTICE, "Initializing log manager with UUID %s", uuid.c_str());
    if (uuid.length() < 10) {
        PANIC("This is a poor choice of a UUID (%s). Refusing to proceed.",
              uuid.c_str());
    }
    if (!storageModule)
        storageModule = Storage::Factory::createStorageModule(config);
    internalLog.reset(storageModule->openLog(INTERNAL_LOG_ID));

    std::vector<LogId> foundLogs = storageModule->getLogs();
    std::deque<const LogEntry*> internalLogEntries =
        internalLog->readFrom(0);

    // Read from the internal log.
    if (internalLog->getLastId() == Storage::NO_ENTRY_ID) {
        for (auto it = foundLogs.begin(); it != foundLogs.end(); ++it) {
            if (*it != INTERNAL_LOG_ID) {
                PANIC("It looks like there's some left-over data stored here, "
                      "but all the metadata is gone?");
            }
        }
        // There's nothing at all here; create the storage.
        initializeStorage();
        return;
    }

    // Replay the internal log.
    replayLogEntries();

    // Delete any logs that shouldn't exist.
    for (auto it = foundLogs.begin(); it != foundLogs.end(); ++it) {
        LogId logId = *it;
        if (logId == INTERNAL_LOG_ID)
            continue;
        if (logs.find(logId) == logs.end())
            storageModule->deleteLog(logId);
    }
}

LogManager::~LogManager()
{
}

void
LogManager::initializeStorage()
{
    LOG(NOTICE, "Initializing your internal log with UUID %s.",
                uuid.c_str());
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid(uuid);
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(/* TODO(ongaro): createTime = */ 0,
                   /* data = */ std::move(data));
    assert(internalLog->getLastId() == Storage::NO_ENTRY_ID);
    internalLog->append(std::move(entry));
    // Replay this metadata entry for good measure.
    replayLogEntries();
}

Storage::LogId
LogManager::createLog(const std::string& logName)
{
    LOG(DBG, "createLog(%s)", logName.c_str());

    // Check if the log already exists.
    auto it = logNames.find(logName);
    if (it != logNames.end()) {
        std::shared_ptr<LogInfo> logInfo = it->second;
        return logInfo->logId;
    }

    // Append to the internal log.
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.DECLARE_LOG_TYPE);
    ProtoBuf::InternalLog::DeclareLog& declareLog =
        *contents.mutable_declare_log();
    declareLog.set_log_name(logName);
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(/* TODO(ongaro): createTime = */ 0,
                   /* data = */ std::move(data));
    LogId logId = internalLog->append(std::move(entry));

    // Replay this new log declaration to actually create the log.
    replayLogEntries();
    return logId;
}

void
LogManager::deleteLog(const std::string& logName)
{
    LOG(DBG, "deleteLog(%s)", logName.c_str());

    // If the log is not in logNames, it has already been deleted.
    auto it = logNames.find(logName);
    if (it == logNames.end())
        return;
    std::shared_ptr<LogInfo> logInfo = it->second;

    // Invalidate the internal log entry.
    LogEntry entry(/* TODO(ongaro): createTime = */ 0,
                   /* invalidations = */ { logInfo->logId });
    internalLog->append(std::move(entry));

    // Replay this new invalidation to actually delete the log.
    replayLogEntries();
}

Core::RWPtr<const Storage::Log>
LogManager::getLogShared(LogId logId) const
{
    auto it = logs.find(logId);
    if (it == logs.end())
        return {};
    std::shared_ptr<LogInfo> logInfo = it->second;
    return logInfo->log.getSharedAccess();
}

Core::RWPtr<Storage::Log>
LogManager::getLogExclusive(LogId logId) const
{
    auto it = logs.find(logId);
    if (it == logs.end())
        return {};
    std::shared_ptr<LogInfo> logInfo = it->second;
    return logInfo->log.getExclusiveAccess();
}

std::vector<std::string>
LogManager::listLogs() const
{
    return DLog::sorted(DLog::getKeys(logNames));
}

void
LogManager::replayLogEntries()
{
    std::deque<const LogEntry*> entries =
        internalLog->readFrom(nextInternalLogEntry);
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const LogEntry& entry = *(*it);
        replayLogEntry(entry);
        nextInternalLogEntry = entry.entryId + 1;
    }
}

void
LogManager::replayLogEntry(const Storage::LogEntry& entry)
{
    const char* MALFORMED_MSG =
        "This probably means you're running an older version of "
        "LogCabin against a log generated by an incompatible newer version.";

    // Replay data
    if (entry.hasData) {
        ProtoBuf::InternalLog::LogEntry contents;
        bool parsed = contents.ParseFromArray(entry.data.getData(),
                                              entry.data.getLength());
        if (!parsed) {
            PANIC("Failed to parse protocol buffer in entry %lu "
                  "of internal log. %s", entry.entryId, MALFORMED_MSG);
        }

        bool entryTypeDoesNotMatchContents = false;
        switch (contents.type()) {
            case ProtoBuf::InternalLog::LogEntry::METADATA_TYPE:
                if (contents.has_metadata())
                    replayMetadataEntry(entry, contents.metadata());
                else
                    entryTypeDoesNotMatchContents = true;
                break;
            case ProtoBuf::InternalLog::LogEntry::DECLARE_LOG_TYPE:
                if (contents.has_declare_log())
                    replayDeclareLogEntry(entry, contents.declare_log());
                else
                    entryTypeDoesNotMatchContents = true;
                break;
            default:
                PANIC("Unknown entry type (%d) in entry %lu of internal log. "
                      " %s", contents.type(), entry.entryId, MALFORMED_MSG);
        }

        if (entryTypeDoesNotMatchContents) {
            PANIC("Entry type (%d) in entry %lu of internal log "
                  "does not match its contents [%s]. "
                  "This is probably a nasty bug.",
                  contents.type(), entry.entryId,
                  Core::ProtoBuf::dumpString(contents, false).c_str());
        }
    }

    // Replay invalidations
    for (auto it = entry.invalidations.begin();
         it != entry.invalidations.end();
         ++it) {
        replayLogInvalidation(*it);
    }
}

void
LogManager::replayLogInvalidation(Storage::EntryId entryId)
{
    // This is probably invalidating a DeclareLog entry -- deleting a log.
    LogId logId = entryId;
    LOG(DBG, "Deleting log %lu", logId);

    auto it = logs.find(logId);
    if (it == logs.end()) {
        // This log doesn't actually exist.
        return;
    }

    std::shared_ptr<LogInfo> logInfo = it->second;
    logInfo->log.reset();
    LOG(DBG, "Log destroyed");
    logNames.erase(logInfo->logName);
    logs.erase(logInfo->logId);

    // The log is now fully deleted as far as anyone can tell. The only thing
    // left is to reclaim its storage resources. If this process crashes, the
    // storage delete will be restarted during initialization when the process
    // comes back up.
    storageModule->deleteLog(logId);
}

void
LogManager::replayMetadataEntry(const Storage::LogEntry& entry,
              const ProtoBuf::InternalLog::Metadata& metadata)
{
    if (metadata.uuid() != this->uuid) {
        PANIC("UUID found in log (%s) does not match the UUID "
              "from the config file (%s)",
              metadata.uuid().c_str(), this->uuid.c_str());
    }
    LOG(DBG, "Confirmed UUID in the metadata entry of the internal log");
}


void
LogManager::replayDeclareLogEntry(const Storage::LogEntry& entry,
              const ProtoBuf::InternalLog::DeclareLog& declareLog)
{
    LogId logId = entry.entryId;
    const std::string& logName = declareLog.log_name();
    LOG(NOTICE, "Creating log %s with ID %lu", logName.c_str(), logId);

    if (logNames.find(logName) != logNames.end()) {
        LOG(NOTICE, "Log already exists");
        return;
    }

    // Create the log.
    std::shared_ptr<LogInfo> logInfo = std::make_shared<LogInfo>(logName);
    logInfo->logId = logId;
    logInfo->log.reset(storageModule->openLog(logId));
    logNames.insert({logName, logInfo});
    logs.insert({logId, logInfo});
}

} // namespace LogCabin::Server
} // namespace LogCabin
