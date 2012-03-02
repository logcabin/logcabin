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
#include <cstring>
#include <string>
#include <gtest/gtest.h>

#include "include/Common.h"
#include "Storage/LogEntry.h"
#include "Storage/MemoryModule.h"

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

class StorageMemoryLogTest : public ::testing::Test {
    StorageMemoryLogTest()
        : log(92)
    {
    }
    MemoryLog log;
};


TEST_F(StorageMemoryLogTest, constructor) {
    EXPECT_EQ(92U, log.logId);
    EXPECT_EQ(NO_ENTRY_ID, log.headId);
    EXPECT_EQ(0U, log.entries.size());
}

TEST_F(StorageMemoryLogTest, getLastId) {
    EXPECT_EQ(NO_ENTRY_ID, log.getLastId());
    EXPECT_EQ(0U, log.append(LogEntry(3, buf("hello"))));
    EXPECT_EQ(0U, log.getLastId());
    EXPECT_EQ(1U, log.append(LogEntry(3, buf("hello"))));
    EXPECT_EQ(1U, log.getLastId());
}

TEST_F(StorageMemoryLogTest, readFrom) {
    EXPECT_EQ(vector<string>{}, eStr(log.readFrom(0)));
    EXPECT_EQ(vector<string>{}, eStr(log.readFrom(12)));
    EXPECT_EQ(0U, log.append(LogEntry(3, buf("hello"))));
    EXPECT_EQ(1U, log.append(LogEntry(6, buf("world!"))));
    EXPECT_EQ((vector<string> {
                "(92, 0) 'hello'",
                "(92, 1) 'world!'",
              }),
              eStr(log.readFrom(0)));
    EXPECT_EQ((vector<string> {
                "(92, 1) 'world!'",
              }),
              eStr(log.readFrom(1)));
    EXPECT_EQ((vector<string> {}),
              eStr(log.readFrom(2)));
}

TEST_F(StorageMemoryLogTest, append) {
    EXPECT_EQ(0U, log.append(LogEntry(3, buf("hello"), {4, 5})));
    EXPECT_EQ("(92, 0) 'hello' [inv 4, 5]",
              log.readFrom(0).at(0)->toString());
    EXPECT_EQ(1U, log.append(LogEntry(3, buf("world!"), {4, 5})));
}

class StorageMemoryModuleTest : public ::testing::Test {
    StorageMemoryModuleTest()
        : sm()
    {
    }
    MemoryModule sm;
};


TEST_F(StorageMemoryModuleTest, getLogs) {
    EXPECT_EQ((vector<LogId>{}),
              DLog::sorted(sm.getLogs()));
    sm.putLog(sm.openLog(38));
    sm.putLog(sm.openLog(755));
    sm.putLog(sm.openLog(129));
    EXPECT_EQ((vector<LogId>{38, 129, 755}),
              DLog::sorted(sm.getLogs()));
}

TEST_F(StorageMemoryModuleTest, openLog) {
    std::unique_ptr<MemoryLog> log(sm.openLog(12));
    EXPECT_EQ(12U, log->logId);
    EXPECT_EQ((vector<LogId>{}),
              DLog::sorted(sm.getLogs()));
    sm.putLog(log.release());
    EXPECT_EQ((vector<LogId>{12}),
              DLog::sorted(sm.getLogs()));
}

TEST_F(StorageMemoryModuleTest, deleteLog) {
    sm.putLog(sm.openLog(12));
    sm.deleteLog(10);
    sm.deleteLog(12);
    EXPECT_EQ((vector<LogId>{}),
              DLog::sorted(sm.getLogs()));
}

TEST_F(StorageMemoryModuleTest, putLog) {
    MemoryLog* log = sm.openLog(12);
    sm.putLog(log);
    delete sm.openLog(10);
    EXPECT_EQ((vector<LogId>{12}),
              DLog::sorted(sm.getLogs()));
    EXPECT_EQ(log, sm.openLog(12));
    delete log;
    EXPECT_EQ((vector<LogId>{}),
              DLog::sorted(sm.getLogs()));
}

} // anonymous namespace
} // namespace LogCabin::Storage
} // namespace LogCabin
