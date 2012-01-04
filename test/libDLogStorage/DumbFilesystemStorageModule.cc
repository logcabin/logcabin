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

#include "Common.h"
#include "Debug.h"
#include "libDLogStorage/DumbFilesystemStorageModule.h"
#include "libDLogStorage/FilesystemUtil.h"

namespace DLog {
namespace Storage {

using std::string;
using std::vector;
using std::deque;

namespace {

class LogAppendCallback : public Log::AppendCallback {
  private:
    LogAppendCallback() = default;
  public:
    void appended(LogEntry entry) {
        lastEntry = entry;
    }
    static LogEntry lastEntry;
};
LogEntry LogAppendCallback::lastEntry {
    0xdeadbeef,
    0xdeadbeef,
    0xdeadbeefdeadbeef,
    Chunk::makeChunk("deadbeef", 9),
    { 0xdeadbeef }
};

class SMOpenCallback : public StorageModule::OpenCallback {
  private:
    explicit SMOpenCallback(Ptr<Log>* result = NULL)
        : result(result)
    {
    }
  public:
    void opened(Ref<Log> log) {
        if (result != NULL)
            *result = log;
    }
    Ptr<Log>* result;
    friend class MakeHelper;
    friend class RefHelper<SMOpenCallback>;
    SMOpenCallback(const SMOpenCallback&) = delete;
    SMOpenCallback& operator=(const SMOpenCallback&) = delete;
};

class SMDeleteCallback : public StorageModule::DeleteCallback {
  private:
    SMDeleteCallback() = default;
  public:
    void deleted(LogId logId) {
        lastLogId = logId;
    }
    static LogId lastLogId;
    friend class MakeHelper;
    friend class RefHelper<SMDeleteCallback>;
};
LogId SMDeleteCallback::lastLogId;

template<typename Container>
vector<string>
eStr(const Container& container)
{
    vector<string> ret;
    for (auto it = container.begin();
         it != container.end();
         ++it) {
        ret.push_back(it->toString());
    }
    return ret;
}

} // anonymous namespace

class DumbFilesystemStorageModuleTest : public ::testing::Test {
  public:
    DumbFilesystemStorageModuleTest()
        : tmpdir(FilesystemUtil::tmpnam())
        , sm() {
        SMDeleteCallback::lastLogId = 0;
    }
    ~DumbFilesystemStorageModuleTest() {
        FilesystemUtil::remove(tmpdir);
    }
    void createStorageModule() {
        sm = make<DumbFilesystemStorageModule>(tmpdir);
    }
    std::string tmpdir;
    Ptr<DumbFilesystemStorageModule> sm;
};

TEST_F(DumbFilesystemStorageModuleTest, constructor) {
    createStorageModule();
    createStorageModule(); // no error if the directory already exists
    EXPECT_DEATH(make<DumbFilesystemStorageModule>(
                            "/the/parent/directory/doesnt/exist"),
                 "Failed to create directory");
}

TEST_F(DumbFilesystemStorageModuleTest, getLogs) {
    createStorageModule();
    EXPECT_EQ((vector<LogId>{}), sorted(sm->getLogs()));
    sm->openLog(38, make<SMOpenCallback>());
    sm->openLog(755, make<SMOpenCallback>());
    sm->openLog(129, make<SMOpenCallback>());
    EXPECT_EQ((vector<LogId>{38, 129, 755}), sorted(sm->getLogs()));
    close(open((tmpdir + "/NaN").c_str(), O_WRONLY|O_CREAT, 0644));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{38, 129, 755}), sorted(sm->getLogs()));
}

TEST_F(DumbFilesystemStorageModuleTest, openLog) {
    createStorageModule();
    Ptr<Log> log;
    sm->openLog(12, make<SMOpenCallback>(&log));
    EXPECT_EQ(12U, log->getLogId());
    EXPECT_EQ((vector<LogId>{12}), sorted(sm->getLogs()));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{12}), sorted(sm->getLogs()));
}

TEST_F(DumbFilesystemStorageModuleTest, deleteLog) {
    createStorageModule();
    Ptr<Log> log;
    sm->openLog(12, make<SMOpenCallback>(&log));
    sm->deleteLog(10, make<SMDeleteCallback>());
    EXPECT_EQ(10U, SMDeleteCallback::lastLogId);
    sm->deleteLog(12, make<SMDeleteCallback>());
    EXPECT_EQ(12U, SMDeleteCallback::lastLogId);
    EXPECT_EQ((vector<LogId>{}), sorted(sm->getLogs()));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{}), sorted(sm->getLogs()));
}

TEST_F(DumbFilesystemStorageModuleTest, getLogPath) {
    createStorageModule();
    EXPECT_EQ(tmpdir + "/000000000000001f", sm->getLogPath(31));
}

class DumbFilesystemLogTest : public DumbFilesystemStorageModuleTest {
  public:
    DumbFilesystemLogTest()
        : log()
    {
        createStorageModule();
        createLog();
    }
    void createLog() {
        Ptr<Log> tmpLog;
        sm->openLog(92, make<SMOpenCallback>(&tmpLog));
        log = Ptr<DumbFilesystemLog>(
                        static_cast<DumbFilesystemLog*>(tmpLog.get()));
    }
    Ptr<DumbFilesystemLog> log;
};

TEST_F(DumbFilesystemLogTest, constructor) {
    EXPECT_EQ(92U, log->getLogId());
    EXPECT_EQ(tmpdir + "/000000000000005c", log->path);
    EXPECT_EQ(NO_ENTRY_ID, log->headId);

    createLog(); // no error if the directory already exists
    EXPECT_DEATH(make<DumbFilesystemLog>(444,
                            "/the/parent/directory/doesnt/exist"),
                 "Failed to create directory");

    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    LogEntry e2(4, 5, 6, Chunk::makeChunk("goodbye", 8));
    log->append(e2, make<LogAppendCallback>());
    createLog();
    EXPECT_EQ((vector<string> {
                "(92, 0) 'hello'",
                "(92, 1) 'goodbye'",
              }),
              eStr(log->entries));
}

TEST_F(DumbFilesystemLogTest, getLastId) {
    EXPECT_EQ(NO_ENTRY_ID, log->getLastId());
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    EXPECT_EQ(0U, log->getLastId());
    log->append(e1, make<LogAppendCallback>());
    EXPECT_EQ(1U, log->getLastId());
    createLog();
    EXPECT_EQ(1U, log->getLastId());
}

TEST_F(DumbFilesystemLogTest, readFrom) {
    EXPECT_EQ(vector<string>{}, eStr(log->readFrom(0)));
    EXPECT_EQ(vector<string>{}, eStr(log->readFrom(12)));
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    LogEntry e2(4, 5, 6, Chunk::makeChunk("world!", 7));
    log->append(e2, make<LogAppendCallback>());
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

TEST_F(DumbFilesystemLogTest, append) {
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6), {4, 5});
    log->append(e1, make<LogAppendCallback>());
    EXPECT_EQ("(92, 0) 'hello' [inv 4, 5]",
              LogAppendCallback::lastEntry.toString());
    LogEntry e2(1, 2, 3, Chunk::makeChunk("goodbye", 8), {4, 5});
    log->append(e2, make<LogAppendCallback>());
    createLog();
    EXPECT_EQ(1U, log->getLastId());
}

TEST_F(DumbFilesystemLogTest, getEntryIds) {
    EXPECT_EQ((vector<EntryId>{}), sorted(log->getEntryIds()));
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    LogEntry e2(4, 5, 6, Chunk::makeChunk("goodbye", 8));
    log->append(e2, make<LogAppendCallback>());
    EXPECT_EQ((vector<LogId>{0, 1}), sorted(log->getEntryIds()));
    close(open((log->path + "/NaN").c_str(), O_WRONLY|O_CREAT, 0644));
    createLog();
    EXPECT_EQ((vector<LogId>{0, 1}), sorted(log->getEntryIds()));
}

TEST_F(DumbFilesystemLogTest, getEntryPath) {
    EXPECT_EQ(log->path + "/000000000000001f", log->getEntryPath(31));
}

TEST_F(DumbFilesystemLogTest, readErrors) {
    EXPECT_DEATH(log->read(444),
                 "Could not open");

    close(open((log->path + "/0000000000000000").c_str(),
               O_WRONLY|O_CREAT, 0644));
    EXPECT_DEATH(log->read(0),
                 "Failed to parse log entry");
}

TEST_F(DumbFilesystemLogTest, writeErrors) {
    LogEntry e1(92, 1, 5, NO_DATA);
    log->write(e1);
    EXPECT_DEATH(log->write(e1),
                 "Could not create");

    // TODO(ongaro): Test a failure in serializing the protocol buffer.
    // I don't see an obvious, clean way to do this.
}

TEST_F(DumbFilesystemLogTest, readWriteCommon) {
    LogEntry e1(92, 1, 5, NO_DATA);
    log->write(e1);
    log->read(1);
    LogEntry e2(92, 2, 6, Chunk::makeChunk("hello", 6));
    log->write(e2);
    log->read(2);
    LogEntry e3(92, 3, 7, NO_DATA, vector<EntryId>{28, 29, 30});
    log->write(e3);
    log->read(3);
    LogEntry e4(92, 4, 8, Chunk::makeChunk("hello", 6),
                vector<EntryId>{31, 33, 94});
    log->write(e4);
    log->read(4);
    // empty data should differ from NO_DATA
    LogEntry e5(92, 5, 9, Chunk::makeChunk("", 0));
    log->write(e5);
    log->read(5);
    EXPECT_EQ((vector<string> {
                "(92, 1) NODATA",
                "(92, 2) 'hello'",
                "(92, 3) NODATA [inv 28, 29, 30]",
                "(92, 4) 'hello' [inv 31, 33, 94]",
                "(92, 5) ''",
              }),
              eStr(log->entries));
    EXPECT_EQ(9U, log->entries.back().createTime);
    EXPECT_EQ(5U, log->headId);
}

} // namespace DLog::Storage
} // namespace DLog
