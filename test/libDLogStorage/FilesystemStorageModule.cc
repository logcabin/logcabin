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
#include "libDLogStorage/FilesystemStorageModule.h"
#include "libDLogStorage/FilesystemUtil.h"
#include "WorkDispatcher.h"

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

void
runWorkerCompletion(uint64_t timeoutMs = 100) {
    while (true) {
        Ptr<WorkDispatcher::CompletionCallback> completion =
                                        workDispatcher->popCompletion();
        if (completion) {
            completion->completed();
            return;
        } else {
            usleep(1000);
            --timeoutMs;
            if (timeoutMs == 0)
                FAIL() << "Exceeded timeout";
        }
    }
}

} // anonymous namespace

class FilesystemStorageModuleTest : public ::testing::Test {
  public:
    FilesystemStorageModuleTest()
        : tmpdir(FilesystemUtil::tmpnam())
        , sm() {
        SMDeleteCallback::lastLogId = 0;
    }
    ~FilesystemStorageModuleTest() {
        FilesystemUtil::remove(tmpdir);
    }
    void createStorageModule() {
        sm = make<FilesystemStorageModule>(tmpdir);
    }
    std::string tmpdir;
    Ptr<FilesystemStorageModule> sm;
};

TEST_F(FilesystemStorageModuleTest, constructor) {
    createStorageModule();
    createStorageModule(); // no error if the directory already exists
    EXPECT_DEATH(make<FilesystemStorageModule>(
                            "/the/parent/directory/doesnt/exist"),
                 "Failed to create directory");
}

TEST_F(FilesystemStorageModuleTest, getLogs) {
    createStorageModule();
    EXPECT_EQ((vector<LogId>{}), sorted(sm->getLogs()));
    sm->openLog(38, make<SMOpenCallback>());
    sm->openLog(755, make<SMOpenCallback>());
    sm->openLog(129, make<SMOpenCallback>());
    runWorkerCompletion();
    runWorkerCompletion();
    runWorkerCompletion();
    EXPECT_EQ((vector<LogId>{38, 129, 755}), sorted(sm->getLogs()));
    close(open((tmpdir + "/NaN").c_str(), O_WRONLY|O_CREAT, 0644));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{38, 129, 755}), sorted(sm->getLogs()));
}

TEST_F(FilesystemStorageModuleTest, openLog) {
    createStorageModule();
    Ptr<Log> log;
    sm->openLog(12, make<SMOpenCallback>(&log));
    runWorkerCompletion();
    EXPECT_EQ(12U, log->getLogId());
    EXPECT_EQ((vector<LogId>{12}), sorted(sm->getLogs()));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{12}), sorted(sm->getLogs()));
}

TEST_F(FilesystemStorageModuleTest, deleteLog) {
    createStorageModule();
    Ptr<Log> log;
    sm->openLog(12, make<SMOpenCallback>(&log));
    runWorkerCompletion();
    sm->deleteLog(10, make<SMDeleteCallback>());
    runWorkerCompletion();
    EXPECT_EQ(10U, SMDeleteCallback::lastLogId);
    sm->deleteLog(12, make<SMDeleteCallback>());
    runWorkerCompletion();
    EXPECT_EQ(12U, SMDeleteCallback::lastLogId);
    EXPECT_EQ((vector<LogId>{}), sorted(sm->getLogs()));
    createStorageModule();
    EXPECT_EQ((vector<LogId>{}), sorted(sm->getLogs()));
}

TEST_F(FilesystemStorageModuleTest, getLogPath) {
    createStorageModule();
    EXPECT_EQ(tmpdir + "/000000000000001f", sm->getLogPath(31));
}

class FilesystemLogTest : public FilesystemStorageModuleTest {
  public:
    FilesystemLogTest()
        : log()
    {
        createStorageModule();
        createLog();
    }
    void createLog() {
        Ptr<Log> tmpLog;
        sm->openLog(92, make<SMOpenCallback>(&tmpLog));
        runWorkerCompletion();
        log = Ptr<FilesystemLog>(
                        static_cast<FilesystemLog*>(tmpLog.get()));
    }
    Ptr<FilesystemLog> log;
};

TEST_F(FilesystemLogTest, constructor) {
    EXPECT_EQ(92U, log->getLogId());
    EXPECT_EQ(tmpdir + "/000000000000005c", log->path);
    EXPECT_EQ(NO_ENTRY_ID, log->headId);

    createLog(); // no error if the directory already exists
    EXPECT_DEATH(make<FilesystemLog>(444,
                            "/the/parent/directory/doesnt/exist"),
                 "Failed to create directory");

    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    runWorkerCompletion();
    LogEntry e2(4, 5, 6, Chunk::makeChunk("goodbye", 8));
    log->append(e2, make<LogAppendCallback>());
    runWorkerCompletion();
    createLog();
    EXPECT_EQ((vector<string> {
                "(92, 0) 'hello'",
                "(92, 1) 'goodbye'",
              }),
              eStr(log->entries));
}

TEST_F(FilesystemLogTest, getLastId) {
    EXPECT_EQ(NO_ENTRY_ID, log->getLastId());
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    runWorkerCompletion();
    EXPECT_EQ(0U, log->getLastId());
    log->append(e1, make<LogAppendCallback>());
    runWorkerCompletion();
    EXPECT_EQ(1U, log->getLastId());
    createLog();
    EXPECT_EQ(1U, log->getLastId());
}

TEST_F(FilesystemLogTest, readFrom) {
    EXPECT_EQ(vector<string>{}, eStr(log->readFrom(0)));
    EXPECT_EQ(vector<string>{}, eStr(log->readFrom(12)));
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    runWorkerCompletion();
    LogEntry e2(4, 5, 6, Chunk::makeChunk("world!", 7));
    log->append(e2, make<LogAppendCallback>());
    runWorkerCompletion();
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

TEST_F(FilesystemLogTest, append) {
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6), {4, 5});
    log->append(e1, make<LogAppendCallback>());
    runWorkerCompletion();
    EXPECT_EQ("(92, 0) 'hello' [inv 4, 5]",
              LogAppendCallback::lastEntry.toString());
    LogEntry e2(1, 2, 3, Chunk::makeChunk("goodbye", 8), {4, 5});
    log->append(e2, make<LogAppendCallback>());
    runWorkerCompletion();
    createLog();
    EXPECT_EQ(1U, log->getLastId());
}

TEST_F(FilesystemLogTest, appendQueuing) {
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    // Test queuing up some writes
    // The first two will be queued in append().
    log->writing = true;
    log->append(e1, make<LogAppendCallback>());
    log->append(e1, make<LogAppendCallback>());
    // append() one more, allowing the three to be written.
    log->writing = false;
    log->append(e1, make<LogAppendCallback>());
    // Before letting the append completion run, queue another one.
    log->append(e1, make<LogAppendCallback>());
    // Now let the first completion run.
    // It should schedule the second batch of writes.
    runWorkerCompletion();
    // And let the second completion run.
    runWorkerCompletion();
    EXPECT_FALSE(log->writing);
    EXPECT_EQ(3U, log->getLastId());
}

TEST_F(FilesystemLogTest, getEntryIds) {
    EXPECT_EQ((vector<EntryId>{}), sorted(log->getEntryIds()));
    LogEntry e1(1, 2, 3, Chunk::makeChunk("hello", 6));
    log->append(e1, make<LogAppendCallback>());
    runWorkerCompletion();
    LogEntry e2(4, 5, 6, Chunk::makeChunk("goodbye", 8));
    log->append(e2, make<LogAppendCallback>());
    runWorkerCompletion();
    EXPECT_EQ((vector<LogId>{0, 1}), sorted(log->getEntryIds()));
    close(open((log->path + "/NaN").c_str(), O_WRONLY|O_CREAT, 0644));
    createLog();
    EXPECT_EQ((vector<LogId>{0, 1}), sorted(log->getEntryIds()));
}

TEST_F(FilesystemLogTest, getEntryPath) {
    EXPECT_EQ(log->path + "/000000000000001f", log->getEntryPath(31));
}

TEST_F(FilesystemLogTest, readErrors) {
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

TEST_F(FilesystemLogTest, writeErrors) {
    LogEntry e1(92, 1, 5, NO_DATA);
    log->write(e1);
    EXPECT_DEATH(log->write(e1),
                 "Could not create"); // File exists
}

TEST_F(FilesystemLogTest, readWriteCommon) {
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
                "(92, 5) BINARY",
              }),
              eStr(log->entries));
    EXPECT_EQ(9U, log->entries.back().createTime);
    EXPECT_EQ(5U, log->headId);
}

} // namespace DLog::Storage
} // namespace DLog
