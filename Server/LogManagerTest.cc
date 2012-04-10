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

#include <string>
#include <gtest/gtest.h>

#include "build/Server/InternalLog.pb.h"
#include "Core/Config.h"
#include "Core/ProtoBuf.h"
#include "Core/STLUtil.h"
#include "RPC/ProtoBuf.h"
#include "Server/LogManager.h"
#include "Storage/LogEntry.h"
#include "Storage/MemoryModule.h"

namespace LogCabin {
namespace Server {
namespace {

using namespace Storage; // NOLINT
using Core::STLUtil::getKeys;
using Core::STLUtil::sorted;
using std::string;
using std::vector;

ProtoBuf::InternalLog::LogEntry
parseEntry(const LogEntry& entry)
{
    ProtoBuf::InternalLog::LogEntry contents;
    EXPECT_TRUE(contents.ParseFromArray(entry.data.getData(),
                                        entry.data.getLength()));
    return contents;
}

class ServerLogManagerTest : public ::testing::Test {
  public:
    ServerLogManagerTest()
        : config()
        , preStorage(new MemoryModule())
        , mgr()
    {
        config.set("uuid", "my-fake-uuid-123");
    }
    void createManager() {
        mgr.reset(new LogManager(config, std::move(preStorage)));
    }
    /**
     * This gives the logs owned by #mgr back to the storage, and moves the
     * storage back to #preStorage.
     */
    void destroyManager() {
        // TODO(ongaro): This could be less dubious.
        preStorage.reset(static_cast<MemoryModule*>(
                                    mgr->storageModule.release()));
        for (auto it = mgr->logs.begin(); it != mgr->logs.end(); ++it) {
            Core::RWManager<Storage::Log>& log = it->second->log;
            preStorage->putLog(static_cast<MemoryLog*>(
                                       log.ptr.release()));
        }
        preStorage->putLog(static_cast<MemoryLog*>(
                                mgr->internalLog.release()));
        mgr.reset();
    }
    MemoryLog* getInternalLog() {
        return static_cast<MemoryLog*>(mgr->internalLog.get());
    }
    Core::Config config;
    std::unique_ptr<MemoryModule> preStorage;
    std::unique_ptr<LogManager> mgr;
};

TEST_F(ServerLogManagerTest, constructor_emptyStorage) {
    createManager();
    EXPECT_EQ("my-fake-uuid-123", mgr->uuid);
    EXPECT_EQ(0U, getInternalLog()->getLastId());
    EXPECT_EQ(1U, mgr->nextInternalLogEntry);
    EXPECT_EQ(0U, mgr->logs.size());
    EXPECT_EQ(0U, mgr->logNames.size());
}

TEST_F(ServerLogManagerTest, constructor_extraLogsWithoutMetadata) {
    preStorage->putLog(preStorage->openLog(13));
    ASSERT_DEATH(createManager(), "left-over data");
}

TEST_F(ServerLogManagerTest, constructor_replayLog) {
    // initialize storage: fully create "foo", "bar" and "baz"; then truncate
    // the internal log after the declare log "bar" entry and delete "bar" from
    // storage.
    createManager();
    EXPECT_EQ(1U, mgr->createLog("foo"));
    EXPECT_EQ(2U, mgr->createLog("bar"));
    EXPECT_EQ(3U, mgr->createLog("baz"));
    destroyManager();
    EXPECT_EQ((vector<LogId> { 0, 1, 2, 3 }),
              sorted(preStorage->getLogs()));
    MemoryLog* log = preStorage->openLog(LogManager::INTERNAL_LOG_ID);
    --log->headId;
    log->entries.pop_back();
    EXPECT_EQ(2U, log->getLastId());
    preStorage->putLog(log);
    preStorage->deleteLog(2); // delete "bar" from storage
    EXPECT_EQ((vector<LogId> { 0, 1, 3 }),
              sorted(preStorage->getLogs()));

    // begin test: the manager should delete the baz log and create the bar log
    createManager();
    EXPECT_EQ(2U, getInternalLog()->getLastId());
    EXPECT_EQ((vector<LogId> { 1, 2 }),
              sorted(getKeys(mgr->logs)));
    EXPECT_EQ((vector<string> {"bar", "foo"}),
              sorted(getKeys(mgr->logNames)));
    EXPECT_EQ("my-fake-uuid-123", mgr->uuid);
    destroyManager();
    EXPECT_EQ((vector<LogId> { 0, 1, 2 }),
              sorted(preStorage->getLogs()));
}

TEST_F(ServerLogManagerTest, initializeStorage) {
    createManager();
    MemoryLog* internalLog = getInternalLog();
    ASSERT_EQ(0U, internalLog->getLastId());
    const LogEntry& entry = *internalLog->readFrom(0).at(0);
    EXPECT_EQ("type: METADATA_TYPE metadata { uuid: 'my-fake-uuid-123' }",
              parseEntry(entry));
}

TEST_F(ServerLogManagerTest, createLog) {
    createManager();
    EXPECT_EQ(1U, mgr->createLog("foo"));
    EXPECT_EQ(1U, getInternalLog()->getLastId());
    EXPECT_EQ((vector<LogId> { 1 }), getKeys(mgr->logs));
    EXPECT_EQ((vector<string> { "foo" }), getKeys(mgr->logNames));
    std::shared_ptr<LogManager::LogInfo> logInfo =
                        mgr->logNames.find("foo")->second;
    EXPECT_EQ("foo", logInfo->logName);
    EXPECT_EQ(1U, logInfo->logId);
    EXPECT_EQ(1U, logInfo->log.getSharedAccess()->logId);

    const LogEntry& entry = *mgr->internalLog->readFrom(1).at(0);
    EXPECT_EQ("type: DECLARE_LOG_TYPE declare_log { log_name: 'foo' }",
              parseEntry(entry));
}

TEST_F(ServerLogManagerTest, createLog_alreadyExists) {
    createManager();
    EXPECT_EQ(1U, mgr->createLog("foo"));
    EXPECT_EQ(1U, mgr->createLog("foo"));
    EXPECT_EQ((vector<LogId> { 1 }), getKeys(mgr->logs));
    EXPECT_EQ((vector<string> { "foo" }), getKeys(mgr->logNames));
    EXPECT_EQ(1U, getInternalLog()->getLastId());
}

TEST_F(ServerLogManagerTest, deleteLog) {
    createManager();
    mgr->createLog("foo");
    mgr->deleteLog("foo");
    EXPECT_EQ(2U, getInternalLog()->getLastId());
    EXPECT_EQ((vector<string> {}), getKeys(mgr->logNames));
    EXPECT_EQ((vector<LogId> {}), getKeys(mgr->logs));
    EXPECT_EQ("(0, 2) NODATA [inv 1]",
              mgr->internalLog->readFrom(2).at(0)->toString());
}

TEST_F(ServerLogManagerTest, deleteLog_doesntExist) {
    createManager();
    mgr->deleteLog("foo");
    EXPECT_EQ(0U, getInternalLog()->getLastId());
    mgr->createLog("bar");
    mgr->deleteLog("bar");
    mgr->deleteLog("bar");
    EXPECT_EQ(2U, getInternalLog()->getLastId());
}

TEST_F(ServerLogManagerTest, getLogShared) {
    createManager();
    EXPECT_TRUE(NULL == mgr->getLogShared(LogManager::INTERNAL_LOG_ID).get());
    EXPECT_TRUE(NULL == mgr->getLogShared(NO_LOG_ID).get());
    EXPECT_TRUE(NULL == mgr->getLogShared(5000).get());
    EXPECT_EQ(1U, mgr->createLog("foo"));
    EXPECT_EQ(1U, mgr->getLogShared(1)->logId);
}

TEST_F(ServerLogManagerTest, getLogExclusive) {
    createManager();
    EXPECT_TRUE(NULL == mgr->getLogExclusive(
                                    LogManager::INTERNAL_LOG_ID).get());
    EXPECT_TRUE(NULL == mgr->getLogExclusive(NO_LOG_ID).get());
    EXPECT_TRUE(NULL == mgr->getLogExclusive(5000).get());
    EXPECT_EQ(1U, mgr->createLog("foo"));
    EXPECT_EQ(1U, mgr->getLogExclusive(1)->logId);
}

TEST_F(ServerLogManagerTest, listLogs) {
    createManager();
    mgr->createLog("foo");
    mgr->createLog("bar");
    mgr->createLog("baz");
    EXPECT_EQ((vector<string> { "bar", "baz", "foo" }),
              mgr->listLogs());
}

TEST_F(ServerLogManagerTest, replayLogEntries) {
    createManager();
    mgr->createLog("foo");
    mgr->createLog("bar");
    mgr->createLog("baz");
    destroyManager();
    createManager();
    EXPECT_EQ((vector<string> { "bar", "baz", "foo" }),
              mgr->listLogs());
    EXPECT_EQ(4U, mgr->nextInternalLogEntry);
    mgr->replayLogEntries();
    EXPECT_EQ(4U, mgr->nextInternalLogEntry);
}

// For replayLogEntry, the normal paths are tested in individual replay*Entry
// tests.

TEST_F(ServerLogManagerTest, replayLogEntryUnparsable) {
    createManager();
    char x[] = "x";
    LogEntry entry(0, RPC::Buffer(x, 1, NULL));
    entry.logId = 0;
    entry.entryId = 1;
    EXPECT_DEATH(mgr->replayLogEntry(entry),
                 "Failed to parse");
}

TEST_F(ServerLogManagerTest, replayLogEntryUnknownType) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.ILLEGAL_TYPE_FOR_TESTING_PURPOSES);
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(0, std::move(data));
    entry.logId = 0;
    entry.entryId = 1;
    EXPECT_DEATH(mgr->replayLogEntry(entry),
                 "Unknown entry type");
}

TEST_F(ServerLogManagerTest, replayLogEntryMismatchedTypeAndContents) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.DECLARE_LOG_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid("failboat");
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(0, std::move(data));
    entry.logId = 0;
    entry.entryId = 1;
    EXPECT_DEATH(mgr->replayLogEntry(entry),
                 "Entry type .* does not match its contents.");
}

TEST_F(ServerLogManagerTest, replayLogInvalidation) {
    createManager();
    EXPECT_EQ(1U, mgr->createLog("foo"));
    LogEntry entry(0, vector<EntryId> {1, 12});
    entry.logId = 0;
    entry.entryId = 1;
    mgr->replayLogEntry(entry);
    EXPECT_EQ((vector<string> {}), getKeys(mgr->logNames));
    EXPECT_EQ((vector<LogId> {}), getKeys(mgr->logs));
    // replaying again should work fine
    mgr->replayLogEntry(entry);
    destroyManager();
    EXPECT_EQ((vector<LogId> { 0 }),
              sorted(preStorage->getLogs()));
}

TEST_F(ServerLogManagerTest, replayMetadataEntry_ok) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid(mgr->uuid); // UUIDs will match
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(0, std::move(data));
    entry.logId = 0;
    entry.entryId = 0;
    mgr->replayLogEntry(entry);
}

TEST_F(ServerLogManagerTest, replayMetadataEntry_badUUID) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid(mgr->uuid + " won't match"); // UUIDs won't match
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(0, std::move(data));
    entry.logId = 0;
    entry.entryId = 0;
    EXPECT_DEATH(mgr->replayLogEntry(entry),
                 "UUID .* does not match");
}

TEST_F(ServerLogManagerTest, replayDeclareLogEntry) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.DECLARE_LOG_TYPE);
    ProtoBuf::InternalLog::DeclareLog& declareLog =
        *contents.mutable_declare_log();
    declareLog.set_log_name("foo");
    RPC::Buffer data;
    RPC::ProtoBuf::serialize(contents, data);
    LogEntry entry(0, std::move(data));
    entry.logId = 0;
    entry.entryId = 1;
    mgr->replayLogEntry(entry);
    EXPECT_EQ((vector<string> { "foo"}), getKeys(mgr->logNames));
    Core::RWPtr<const Log> log = mgr->getLogShared(1);
    EXPECT_TRUE(NULL != log.get());
    // replaying again should not create a new Log object
    mgr->replayLogEntry(entry);
    EXPECT_EQ(log.get(), mgr->getLogShared(1).get());
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
