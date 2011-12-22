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

#include <string>
#include <gtest/gtest.h>

#include "Common.h"
#include "DLogStorage.h"
#include "libDLogStorage/MemoryStorageModule.h"

namespace DLog {
namespace Storage {

using std::string;
using std::vector;

TEST(Chunk, makeChunk) {
    Ref<Chunk> chunk1 = Chunk::makeChunk("blah", 0);
    EXPECT_EQ(0U, chunk1->getLength());

    const char* hello = "hello";
    Ref<Chunk> chunk2 = Chunk::makeChunk(hello, 6);
    EXPECT_EQ(6U, chunk2->getLength());
    // chunk should have copied its input
    EXPECT_STREQ("hello", static_cast<const char*>(chunk2->getData()));
    EXPECT_NE("hello", static_cast<const char*>(chunk2->getData()));
}

TEST(LogEntry, constructorWithData) {
    Ref<Chunk> data = Chunk::makeChunk("hello", 6);
    LogEntry entry(1, 2, 3, data, {4, 5, 6});
    EXPECT_EQ(1U, entry.logId);
    EXPECT_EQ(2U, entry.entryId);
    EXPECT_EQ(3U, entry.createTime);
    EXPECT_EQ((vector<EntryId>{4, 5, 6}), entry.invalidations);
    EXPECT_EQ(data, entry.data);
}

TEST(LogEntry, constructorWithoutData) {
    LogEntry entry(1, 2, 3, {4, 5, 6});
    EXPECT_EQ(1U, entry.logId);
    EXPECT_EQ(2U, entry.entryId);
    EXPECT_EQ(3U, entry.createTime);
    EXPECT_EQ((vector<EntryId>{4, 5, 6}), entry.invalidations);
    EXPECT_EQ(NO_DATA, entry.data);
}

TEST(LogEntry, toString) {
    Ref<Chunk> data = Chunk::makeChunk("hello", 6);
    LogEntry entry1(1, 2, 3, data, {4, 5, 6});
    LogEntry entry2(1, 2, 3, {4, 5, 6});
    LogEntry entry3(1, 2, 3, data);
    LogEntry entry4(1, 2, 3, Chunk::makeChunk("hi", 0));
    LogEntry entry5(1, 2, 3, Chunk::makeChunk("\n", 1));
    EXPECT_EQ("(1, 2) 'hello' [inv 4, 5, 6]", entry1.toString());
    EXPECT_EQ("(1, 2) NODATA [inv 4, 5, 6]", entry2.toString());
    EXPECT_EQ("(1, 2) 'hello'", entry3.toString());
    EXPECT_EQ("(1, 2) ''", entry4.toString());
    EXPECT_EQ("(1, 2) BINARY", entry5.toString());
}


// The Log tests use MemoryLog since Log is an abstract class.

TEST(Log, constructor) {
    Ref<Log> log = make<MemoryLog>(38);
    EXPECT_EQ(38U, log->getLogId());
}

namespace {

// used in Log's destructor test
class LogDestructorCallback : public Log::DestructorCallback {
  private:
      LogDestructorCallback() = default;
  public:
    void destructorCallback(LogId logId) {
        ++count;
        lastLogId = logId;
    }
    static uint32_t count;
    static LogId lastLogId;
    friend class MakeHelper;
    friend class RefHelper<LogDestructorCallback>;
};
uint32_t LogDestructorCallback::count;
LogId LogDestructorCallback::lastLogId;

} // anonymous namespace

TEST(Log, destructor) {
    LogDestructorCallback::count = 0;
    LogDestructorCallback::lastLogId = 0;
    {
        Ref<Log> log = make<MemoryLog>(38);
        log->addDestructorCallback(make<LogDestructorCallback>());
        log->addDestructorCallback(make<LogDestructorCallback>());
    }
    EXPECT_EQ(2U, LogDestructorCallback::count);
    EXPECT_EQ(38U, LogDestructorCallback::lastLogId);
}

// addDestructorCallback tested as part of destructor


} // namespace DLog::Storage
} // namespace DLog
