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

#include "Common.h"
#include "Config.h"
#include "dlogd/LogManager.h"
#include "libDLogStorage/MemoryStorageModule.h"

namespace DLog {

using namespace DLog::Storage; // NOLINT
using std::string;
using std::vector;

namespace {

class InitializeCallback : public LogManager::InitializeCallback {
  private:
    InitializeCallback() = default;
  public:
    void initialized() {
        count++;
    }
    static uint32_t count;
    friend class MakeHelper;
    friend class RefHelper<InitializeCallback>;
};
uint32_t InitializeCallback::count;

class CreateCallback : public LogManager::CreateCallback {
  private:
    CreateCallback() = default;
  public:
    void created(LogId logId) {
        count++;
        lastLogId = logId;
    }
    static uint32_t count;
    static LogId lastLogId;
    friend class MakeHelper;
    friend class RefHelper<CreateCallback>;
};
uint32_t CreateCallback::count;
LogId CreateCallback::lastLogId;

class DeleteCallback : public LogManager::DeleteCallback {
  private:
    DeleteCallback() = default;
  public:
    void deleted() {
        count++;
    }
    static uint32_t count;
    friend class MakeHelper;
    friend class RefHelper<DeleteCallback>;
};
uint32_t DeleteCallback::count;

class ReplayCallback : public LogManager::ReplayCallback {
  private:
    ReplayCallback() = default;
  public:
    void replayed() {
        count++;
    }
    static uint32_t count;
    friend class MakeHelper;
    friend class RefHelper<ReplayCallback>;
};
uint32_t ReplayCallback::count;

class NoOpStorageDeleteCallback : public StorageModule::DeleteCallback {
  private:
    NoOpStorageDeleteCallback() = default;
  public:
    void deleted(LogId logId) {}
    friend class RefHelper<NoOpStorageDeleteCallback>;
    friend class MakeHelper;
};

} // anonymous namespace

class LogManagerTest : public ::testing::Test {
  public:
    LogManagerTest()
        : config()
        , storage(make<MemoryStorageModule>())
        , mgr()
    {
        config.set("uuid", "my-fake-uuid-123");
        InitializeCallback::count = 0;
        CreateCallback::count = 0;
        CreateCallback::lastLogId = 0;
        DeleteCallback::count = 0;
        ReplayCallback::count = 0;
    }
    void createManager() {
        // This is counting on the memory storage to initialize right away.
        mgr = make<LogManager>(config,
                               storage,
                               make<InitializeCallback>());
    }
    Ptr<MemoryLog> getInternalLog() {
        auto it = storage->logs.find(LogManager::INTERNAL_LOG_ID);
        if (it == storage->logs.end())
            return Ptr<MemoryLog>();
        Ptr<Log> log = it->second;
        return Ptr<MemoryLog>(static_cast<MemoryLog*>(log.get()));
    }
    Config config;
    Ref<MemoryStorageModule> storage;
    Ptr<LogManager> mgr;
};

TEST_F(LogManagerTest, constructor_emptyStorage) {
    createManager();
    EXPECT_EQ(1U, InitializeCallback::count);
    EXPECT_EQ(0U, getInternalLog()->getLastId());
    EXPECT_TRUE(mgr->initialized);
    EXPECT_EQ(0U, mgr->logs.size());
    EXPECT_EQ(0U, mgr->logNames.size());
    EXPECT_EQ("my-fake-uuid-123", mgr->uuid);
}

TEST_F(LogManagerTest, constructor_extraLogsWithoutMetadata) {
    storage->logs.insert({13, make<MemoryLog>(13)});
    ASSERT_DEATH(createManager(), "left-over data");
}

TEST_F(LogManagerTest, constructor_replayLog) {
    // initialize storage: fully create "foo", "bar" and "baz"; then truncate
    // the internal log after the declare log "bar" entry and delete "bar" from
    // storage.
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    mgr->createLog("bar", make<CreateCallback>());
    mgr->createLog("baz", make<CreateCallback>());
    mgr.reset();
    Ptr<MemoryLog> log(getInternalLog());
    --log->headId;
    log->entries.pop_back();
    EXPECT_EQ(2U, log->getLastId());
    storage->deleteLog(2, make<NoOpStorageDeleteCallback>());
    InitializeCallback::count = 0;

    // begin test: the manager should delete the bar entry and create the foo
    // entry
    createManager();
    EXPECT_EQ(1U, InitializeCallback::count);
    EXPECT_EQ(2U, log->getLastId());
    EXPECT_EQ(2U, getInternalLog()->getLastId());
    EXPECT_TRUE(mgr->initialized);
    EXPECT_EQ((vector<LogId> { 1, 2 }), getKeys(mgr->logs));
    EXPECT_EQ((vector<string> {"foo", "bar"}), getKeys(mgr->logNames));
    EXPECT_EQ("my-fake-uuid-123", mgr->uuid);
}

TEST_F(LogManagerTest, initializeStorage) {
    createManager();
    EXPECT_EQ(1U, InitializeCallback::count);
    EXPECT_TRUE(mgr->initialized);
    Ptr<Log> internalLog = getInternalLog();
    ASSERT_EQ(0U, internalLog->getLastId());
    LogEntry entry = internalLog->readFrom(0).at(0);
    EXPECT_EQ("(0, 0) BINARY", entry.toString());
    ProtoBuf::InternalLog::LogEntry contents;
    ASSERT_TRUE(contents.ParseFromArray(entry.data->getData(),
                                        entry.data->getLength()));
    EXPECT_EQ("type: METADATA_TYPE metadata { uuid: \"my-fake-uuid-123\" }",
              contents.ShortDebugString());
}

TEST_F(LogManagerTest, createLog) {
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    EXPECT_EQ(1U, CreateCallback::count);
    EXPECT_EQ(1U, CreateCallback::lastLogId);
    EXPECT_EQ(1U, getInternalLog()->getLastId());
    EXPECT_EQ((vector<LogId> { 1 }), getKeys(mgr->logs));
    EXPECT_EQ((vector<string> { "foo" }), getKeys(mgr->logNames));
    Ref<LogManager::LogInfo> logInfo = mgr->logNames.find("foo")->second;
    EXPECT_EQ("foo", logInfo->logName);
    EXPECT_EQ(1U, logInfo->logId);
    EXPECT_EQ(1U, logInfo->log->logId);

    ASSERT_EQ(1U, mgr->internalLog->getLastId());
    LogEntry entry = mgr->internalLog->readFrom(1).at(0);
    EXPECT_EQ("(0, 1) BINARY", entry.toString());
    ProtoBuf::InternalLog::LogEntry contents;
    ASSERT_TRUE(contents.ParseFromArray(entry.data->getData(),
                                        entry.data->getLength()));
    EXPECT_EQ("type: DECLARE_LOG_TYPE declare_log { log_name: \"foo\" }",
              contents.ShortDebugString());
}

TEST_F(LogManagerTest, createLogAlreadyExists) {
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    EXPECT_EQ(1U, CreateCallback::lastLogId);
    mgr->createLog("foo", make<CreateCallback>());
    EXPECT_EQ(1U, CreateCallback::lastLogId);
    EXPECT_EQ(2U, CreateCallback::count);
    EXPECT_EQ((vector<LogId> { 1 }), getKeys(mgr->logs));
    EXPECT_EQ((vector<string> { "foo" }), getKeys(mgr->logNames));
}

TEST_F(LogManagerTest, deleteLog) {
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    // The in-memory storage module keeps a ref to each log for convenience in
    // other tests, but this blocks the log from being fully deleted.
    static_cast<MemoryStorageModule*>(mgr->storageModule.get())->logs.clear();
    EXPECT_EQ(1U, mgr->logNames.find("foo")->second->logId);
    mgr->deleteLog("foo", make<DeleteCallback>());
    EXPECT_EQ(1U, DeleteCallback::count);
    EXPECT_EQ((vector<string> {}), getKeys(mgr->logNames));
    EXPECT_EQ((vector<LogId> {}), getKeys(mgr->logs));
}

TEST_F(LogManagerTest, deleteLogDoesntExist) {
    createManager();
    mgr->deleteLog("foo", make<DeleteCallback>());
    EXPECT_EQ(1U, DeleteCallback::count);
}

TEST_F(LogManagerTest, deleteLogConcurrentlyDeleted) {
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    mgr->logNames.find("foo")->second->log.reset();
    mgr->deleteLog("foo", make<DeleteCallback>());
    EXPECT_EQ(1U, DeleteCallback::count);
}

TEST_F(LogManagerTest, getLog) {
    createManager();
    EXPECT_EQ(Ptr<Log>(), mgr->getLog(LogManager::INTERNAL_LOG_ID));
    EXPECT_EQ(Ptr<Log>(), mgr->getLog(NO_LOG_ID));
    EXPECT_EQ(Ptr<Log>(), mgr->getLog(5000));
    mgr->createLog("foo", make<CreateCallback>());
    EXPECT_EQ(1U, CreateCallback::lastLogId);
    EXPECT_EQ(1U, mgr->getLog(1)->getLogId());
}

TEST_F(LogManagerTest, listLogs) {
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    mgr->createLog("bar", make<CreateCallback>());
    mgr->createLog("baz", make<CreateCallback>());
    EXPECT_EQ((vector<string> { "bar", "baz", "foo" }),
              mgr->listLogs());
}


// For replayLogEntry, the normal paths are tested in individual replay*Entry
// tests.

TEST_F(LogManagerTest, replayLogEntryOneInvalidationXorData) {
    createManager();
    LogEntry entry1(0, 1, 0, NO_DATA, vector<EntryId>{});
    EXPECT_DEATH(mgr->replayLogEntry(entry1, make<ReplayCallback>()),
                 "one invalidation xor some data");
    LogEntry entry2(0, 1, 0, NO_DATA, vector<EntryId>{1, 2});
    EXPECT_DEATH(mgr->replayLogEntry(entry2, make<ReplayCallback>()),
                 "one invalidation xor some data");
    LogEntry entry3(0, 1, 0, Chunk::makeChunk("x", 2), vector<EntryId>{1});
    EXPECT_DEATH(mgr->replayLogEntry(entry3, make<ReplayCallback>()),
                 "one invalidation xor some data");
}

TEST_F(LogManagerTest, replayLogEntryUnparsable) {
    createManager();
    LogEntry entry(0, 1, 0, Chunk::makeChunk("x", 2));
    EXPECT_DEATH(mgr->replayLogEntry(entry, make<ReplayCallback>()),
                 "Failed to parse");
}

TEST_F(LogManagerTest, replayLogEntryOtherAt0) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.DECLARE_LOG_TYPE);
    LogEntry entry(0, 0, 0, Chunk::makeChunk(contents));
    EXPECT_DEATH(mgr->replayLogEntry(entry, make<ReplayCallback>()),
                 "EntryId 0 should be a Metadata entry");
}

TEST_F(LogManagerTest, replayLogEntryMetadataAfter0) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    LogEntry entry(0, 1, 0, Chunk::makeChunk(contents));
    EXPECT_DEATH(mgr->replayLogEntry(entry, make<ReplayCallback>()),
                 "Metadata entry should only appear at entryId 0");
}

TEST_F(LogManagerTest, replayLogEntryUnknownType) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.ILLEGAL_TYPE_FOR_TESTING_PURPOSES);
    LogEntry entry(0, 1, 0, Chunk::makeChunk(contents));
    EXPECT_DEATH(mgr->replayLogEntry(entry, make<ReplayCallback>()),
                 "Unknown entry type");
}

TEST_F(LogManagerTest, replayLogEntryMismatchedTypeAndContents) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.DECLARE_LOG_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid("failboat");
    LogEntry entry(0, 1, 0, Chunk::makeChunk(contents));
    EXPECT_DEATH(mgr->replayLogEntry(entry, make<ReplayCallback>()),
                 "Entry type .* does not match its contents.");
}

TEST_F(LogManagerTest, replayInvalidation) {
    createManager();
    mgr->createLog("foo", make<CreateCallback>());
    // The in-memory storage module keeps a ref to each log for convenience in
    // other tests, but this blocks the log from being fully deleted.
    static_cast<MemoryStorageModule*>(mgr->storageModule.get())->logs.clear();
    LogEntry entry(0, 1, 0, vector<EntryId> {1});
    mgr->replayLogEntry(entry, make<ReplayCallback>());
    EXPECT_EQ(1U, ReplayCallback::count);
    EXPECT_EQ((vector<string> {}), getKeys(mgr->logNames));
    EXPECT_EQ((vector<LogId> {}), getKeys(mgr->logs));
    // replaying again should work fine
    mgr->replayLogEntry(entry, make<ReplayCallback>());
    EXPECT_EQ(2U, ReplayCallback::count);
}

TEST_F(LogManagerTest, replayMetadataEntryOk) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid(mgr->uuid); // UUIDs will match
    LogEntry entry(0, 0, 0, Chunk::makeChunk(contents));
    mgr->replayLogEntry(entry, make<ReplayCallback>());
    EXPECT_EQ(1U, ReplayCallback::count);
}

TEST_F(LogManagerTest, replayMetadataEntryBadUUID) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.METADATA_TYPE);
    ProtoBuf::InternalLog::Metadata& metadata = *contents.mutable_metadata();
    metadata.set_uuid(mgr->uuid + " won't match"); // UUIDs won't match
    LogEntry entry(0, 0, 0, Chunk::makeChunk(contents));
    EXPECT_DEATH(mgr->replayLogEntry(entry, make<ReplayCallback>()),
                 "UUID .* does not match");
}

TEST_F(LogManagerTest, replayDeclareLogEntry) {
    createManager();
    ProtoBuf::InternalLog::LogEntry contents;
    contents.set_type(contents.DECLARE_LOG_TYPE);
    ProtoBuf::InternalLog::DeclareLog& declareLog =
        *contents.mutable_declare_log();
    declareLog.set_log_name("foo");
    LogEntry entry(0, 1, 0, Chunk::makeChunk(contents));
    mgr->replayLogEntry(entry, make<ReplayCallback>());
    EXPECT_EQ(1U, ReplayCallback::count);
    EXPECT_EQ((vector<string> { "foo"}), getKeys(mgr->logNames));
    Ptr<Log> log = mgr->getLog(1);
    EXPECT_NE(Ptr<Log>(), log);
    // replaying again should not create a new Log object
    // The warning produced by the following line is expected.
    mgr->replayLogEntry(entry, make<ReplayCallback>());
    EXPECT_EQ(2U, ReplayCallback::count);
    EXPECT_EQ(log, mgr->getLog(1));
}

} // namespace DLog
