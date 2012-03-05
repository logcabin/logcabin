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

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtest/gtest.h>

#include "include/Common.h"
#include "Core/Debug.h"
#include "Core/Config.h"
#include "Storage/FilesystemModule.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Storage {
namespace {


using std::string;
using std::vector;
using std::deque;

/// Call toString() on a list of entries.
vector<string>
eStr(const std::deque<const LogEntry*>& entries)
{
    vector<string> ret;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const LogEntry* entry = *it;
        ret.push_back(entry->toString());
    }
    return ret;
}

RPC::Buffer
buf(const char* str)
{
    return RPC::Buffer(strdup(str),
                       uint32_t(strlen(str) + 1),
                       free);
}

class StorageFilesystemModuleTest : public ::testing::Test {
  public:
    StorageFilesystemModuleTest()
        : tmpdir(FilesystemUtil::tmpnam())
        , config()
        , sm() {
        config.set("storagePath", tmpdir);
    }
    ~StorageFilesystemModuleTest() {
        FilesystemUtil::remove(tmpdir);
    }
    void createStorageModule() {
        sm.reset(new FilesystemModule(config));
    }
    std::string tmpdir;
    Core::Config config;
    std::unique_ptr<FilesystemModule> sm;
};

TEST_F(StorageFilesystemModuleTest, constructor) {
    createStorageModule();
    createStorageModule(); // no error if the directory already exists
    Core::Config c;
    c.set("storagePath", "/the/parent/directory/doesnt/exist");
    EXPECT_DEATH(FilesystemModule x(c),
                 "Failed to create directory");
}

TEST_F(StorageFilesystemModuleTest, getLogs) {
    createStorageModule();
    EXPECT_EQ((vector<LogId>{}), DLog::sorted(sm->getLogs()));
    delete sm->openLog(38);
    delete sm->openLog(755);
    delete sm->openLog(129);
    EXPECT_EQ((vector<LogId>{38, 129, 755}), DLog::sorted(sm->getLogs()));
    close(open((tmpdir + "/NaN").c_str(), O_WRONLY|O_CREAT, 0644));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{38, 129, 755}), DLog::sorted(sm->getLogs()));
}

TEST_F(StorageFilesystemModuleTest, openLog) {
    createStorageModule();
    std::unique_ptr<Log> log(sm->openLog(12));
    EXPECT_EQ(12U, log->logId);
    log.reset();
    EXPECT_EQ((vector<LogId>{12}), DLog::sorted(sm->getLogs()));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{12}), DLog::sorted(sm->getLogs()));
}

TEST_F(StorageFilesystemModuleTest, deleteLog) {
    createStorageModule();
    delete sm->openLog(12);
    sm->deleteLog(10);
    sm->deleteLog(12);
    EXPECT_EQ((vector<LogId>{}), DLog::sorted(sm->getLogs()));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{}), DLog::sorted(sm->getLogs()));
}

TEST_F(StorageFilesystemModuleTest, getLogPath) {
    createStorageModule();
    EXPECT_EQ(tmpdir + "/000000000000001f", sm->getLogPath(31));
}

class StorageFilesystemLogTest : public StorageFilesystemModuleTest {
  public:
    StorageFilesystemLogTest()
        : log()
    {
        createStorageModule();
        createLog();
    }
    void createLog() {
        log.reset(static_cast<FilesystemLog*>(sm->openLog(92)));
    }
    std::unique_ptr<FilesystemLog> log;
};

TEST_F(StorageFilesystemLogTest, constructor) {
    EXPECT_EQ(92U, log->logId);
    EXPECT_EQ(tmpdir + "/000000000000005c", log->path);
    EXPECT_EQ(NO_ENTRY_ID, log->headId);

    createLog(); // no error if the directory already exists
    EXPECT_DEATH(FilesystemLog(444,
                            "/the/parent/directory/doesnt/exist",
                            "SHA-1"),
                 "Failed to create directory");

    EXPECT_EQ(0U, log->append(LogEntry(3, buf("hello"))));
    EXPECT_EQ(1U, log->append(LogEntry(6, buf("goodbye"))));
    createLog();
    EXPECT_EQ((vector<string> {
                "(92, 0) 'hello'",
                "(92, 1) 'goodbye'",
              }),
              eStr(log->readFrom(0)));
}

TEST_F(StorageFilesystemLogTest, getLastId) {
    EXPECT_EQ(NO_ENTRY_ID, log->getLastId());
    EXPECT_EQ(0U, log->append(LogEntry(3, buf("hello"))));
    EXPECT_EQ(0U, log->getLastId());
    EXPECT_EQ(1U, log->append(LogEntry(3, buf("hello"))));
    EXPECT_EQ(1U, log->getLastId());
    createLog();
    EXPECT_EQ(1U, log->getLastId());
}

TEST_F(StorageFilesystemLogTest, readFrom) {
    EXPECT_EQ(vector<string>{}, eStr(log->readFrom(0)));
    EXPECT_EQ(vector<string>{}, eStr(log->readFrom(12)));
    log->append(LogEntry(3, buf("hello")));
    log->append(LogEntry(6, buf("world!")));
    EXPECT_EQ((vector<string> {
                "(92, 0) 'hello'",
                "(92, 1) 'world!'",
              }),
              eStr(log->readFrom(0)));
    EXPECT_EQ((vector<string> {
                "(92, 1) 'world!'",
              }),
              eStr(log->readFrom(1)));
    EXPECT_EQ((vector<string> {}),
              eStr(log->readFrom(2)));
    createLog();
    EXPECT_EQ((vector<string> {
                "(92, 0) 'hello'",
                "(92, 1) 'world!'",
              }),
              eStr(log->readFrom(0)));
}

TEST_F(StorageFilesystemLogTest, append) {
    EXPECT_EQ(0U, log->append(LogEntry(3, buf("hello"), {4, 5})));
    EXPECT_EQ("(92, 0) 'hello' [inv 4, 5]",
              log->entries.back().toString());
    EXPECT_EQ(1U, log->append(LogEntry(3, buf("goodbye"))));
    createLog();
    EXPECT_EQ(1U, log->getLastId());
}

TEST_F(StorageFilesystemLogTest, getEntryIds) {
    EXPECT_EQ((vector<EntryId>{}),
              DLog::sorted(log->getEntryIds()));
    log->append(LogEntry(3, buf("hello")));
    log->append(LogEntry(6, buf("goodbye")));
    EXPECT_EQ((vector<LogId>{0, 1}),
              DLog::sorted(log->getEntryIds()));
    close(open((log->path + "/NaN").c_str(), O_WRONLY|O_CREAT, 0644));
    createLog();
    EXPECT_EQ((vector<LogId>{0, 1}),
              DLog::sorted(log->getEntryIds()));
}

TEST_F(StorageFilesystemLogTest, getEntryPath) {
    EXPECT_EQ(log->path + "/000000000000001f", log->getEntryPath(31));
}

TEST_F(StorageFilesystemLogTest, readErrors) {
    // File does not exist
    EXPECT_DEATH(log->read(444),
                 "Could not open");

    // Empty file
    close(open((log->path + "/0000000000000000").c_str(),
               O_WRONLY|O_CREAT, 0644));
    EXPECT_DEATH(log->read(0),
                 "File .* corrupt");

    // TODO(ongaro): do some random testing
}

TEST_F(StorageFilesystemLogTest, writeErrors) {
    LogEntry e1(5, std::vector<EntryId>{});
    e1.logId = 92;
    e1.entryId = 1;
    log->write(e1);
    EXPECT_DEATH(log->write(e1),
                 "Could not create"); // File exists
}

TEST_F(StorageFilesystemLogTest, readWriteCommon) {
    LogEntry e1(5, std::vector<EntryId>{});
    e1.logId = 92;
    e1.entryId = 1;
    log->write(e1);
    log->read(1);
    LogEntry e2(6, buf("hello"));
    e2.logId = 92;
    e2.entryId = 2;
    log->write(e2);
    log->read(2);
    LogEntry e3(7, std::vector<EntryId>{28, 29, 30});
    e3.logId = 92;
    e3.entryId = 3;
    log->write(e3);
    log->read(3);
    LogEntry e4(8, buf("hello"), std::vector<EntryId>{31, 33, 94});
    e4.logId = 92;
    e4.entryId = 4;
    log->write(e4);
    log->read(4);
    // empty data should differ from NO_DATA
    LogEntry e5(9, RPC::Buffer());
    e5.logId = 92;
    e5.entryId = 5;
    log->write(e5);
    log->read(5);
    EXPECT_EQ((vector<string> {
                "(92, 1) NODATA",
                "(92, 2) 'hello'",
                "(92, 3) NODATA [inv 28, 29, 30]",
                "(92, 4) 'hello' [inv 31, 33, 94]",
                "(92, 5) BINARY",
              }),
              eStr(log->readFrom(0)));
    EXPECT_EQ(9U, log->entries.back().createTime);
    EXPECT_EQ(5U, log->headId);
}

} // namespace LogCabin::Storage::<anonymous>
} // namespace LogCabin::Storage
} // namespace LogCabin
