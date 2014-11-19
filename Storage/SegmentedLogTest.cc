#if 0
/* Copyright (c) 2012-2014 Stanford University
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

#include <gtest/gtest.h>
#include <stdexcept>

#include "Storage/FilesystemUtil.h"
#include "Storage/SegmentedLog.h"

namespace LogCabin {
namespace Storage {
namespace {

namespace FS = FilesystemUtil;

uint64_t
numEntries(const Log& log)
{
    return log.getLastLogIndex() + 1 - log.getLogStartIndex();
}

class StorageSegmentedLogTest : public ::testing::Test {
    StorageSegmentedLogTest()
        : tmpDir(FS::openDir(FS::mkdtemp()))
        , log(new SegmentedLog(tmpDir))
        , sampleEntry()
    {
        sampleEntry.set_type(Protocol::Raft::EntryType::DATA);
        sampleEntry.set_term(40);
        sampleEntry.set_data("foo");
    }
    ~StorageSegmentedLogTest()
    {
        log.reset();
        FS::remove(tmpDir.path);
    }
    FS::File tmpDir;
    std::unique_ptr<SegmentedLog> log;
    Log::Entry sampleEntry;
};

TEST_F(StorageSegmentedLogTest, basic)
{
    std::unique_ptr<Log::Sync> sync = log->appendSingle(sampleEntry);
    EXPECT_EQ(1U, sync->firstIndex);
    EXPECT_EQ(1U, sync->lastIndex);
    Log::Entry entry = log->getEntry(1);
    EXPECT_EQ(40U, entry.term());
    EXPECT_EQ("foo", entry.data());
}

TEST_F(StorageSegmentedLogTest, append)
{
    std::unique_ptr<Log::Sync> sync = log->appendSingle(sampleEntry);
    EXPECT_EQ(1U, sync->firstIndex);
    log->truncatePrefix(10);
    sync = log->appendSingle(sampleEntry);
    EXPECT_EQ(10U, sync->firstIndex);
}

TEST_F(StorageSegmentedLogTest, getEntry)
{
    log->appendSingle(sampleEntry);
    Log::Entry entry = log->getEntry(1);
    EXPECT_EQ(40U, entry.term());
    EXPECT_EQ("foo", entry.data());
    EXPECT_DEATH(log->getEntry(0), "outside of log");
    EXPECT_DEATH(log->getEntry(2), "outside of log");

    sampleEntry.set_data("bar");
    log->appendSingle(sampleEntry);
    log->truncatePrefix(2);
    EXPECT_DEATH(log->getEntry(1), "outside of log");
    log->appendSingle(sampleEntry);
    Log::Entry entry2 = log->getEntry(2);
    EXPECT_EQ("bar", entry2.data());
}

TEST_F(StorageSegmentedLogTest, getLogStartIndex)
{
    EXPECT_EQ(1U, log->getLogStartIndex());
    log->truncatePrefix(200);
    log->truncatePrefix(100);
    EXPECT_EQ(200U, log->getLogStartIndex());
}

TEST_F(StorageSegmentedLogTest, getLastLogIndex)
{
    EXPECT_EQ(0U, log->getLastLogIndex());
    log->appendSingle(sampleEntry);
    log->appendSingle(sampleEntry);
    EXPECT_EQ(2U, log->getLastLogIndex());

    log->truncatePrefix(2);
    EXPECT_EQ(2U, log->getLastLogIndex());
}

TEST_F(StorageSegmentedLogTest, getSizeBytes)
{
    EXPECT_EQ(0U, log->getSizeBytes());
    log->appendSingle(sampleEntry);
    uint64_t s = log->getSizeBytes();
    EXPECT_LT(0U, s);
    log->appendSingle(sampleEntry);
    EXPECT_EQ(2 * s, log->getSizeBytes());
}

TEST_F(StorageSegmentedLogTest, truncatePrefix)
{
    EXPECT_EQ(1U, log->getLogStartIndex());
    log->truncatePrefix(0);
    EXPECT_EQ(1U, log->getLogStartIndex());
    log->truncatePrefix(1);
    EXPECT_EQ(1U, log->getLogStartIndex());

    // case 1: entries is empty
    log->truncatePrefix(500);
    EXPECT_EQ(500U, log->getLogStartIndex());
    EXPECT_EQ(0U, numEntries(*log));

    // case 2: entries has fewer elements than truncated
    log->appendSingle(sampleEntry);
    log->truncatePrefix(502);
    EXPECT_EQ(502U, log->getLogStartIndex());
    EXPECT_EQ(0U, numEntries(*log));

    // case 3: entries has exactly the elements truncated
    log->appendSingle(sampleEntry);
    log->appendSingle(sampleEntry);
    log->truncatePrefix(504);
    EXPECT_EQ(504U, log->getLogStartIndex());
    EXPECT_EQ(0U, numEntries(*log));

    // case 4: entries has more elements than truncated
    log->appendSingle(sampleEntry);
    log->appendSingle(sampleEntry);
    sampleEntry.set_data("bar");
    log->appendSingle(sampleEntry);
    log->truncatePrefix(506);
    EXPECT_EQ(506U, log->getLogStartIndex());
    EXPECT_EQ(1U, numEntries(*log));
    EXPECT_EQ("bar", log->getEntry(506).data());

    // make sure truncating to an earlier id has no effect
    EXPECT_EQ(1U, numEntries(*log));
    log->truncatePrefix(400);
    EXPECT_EQ(506U, log->getLogStartIndex());
}

TEST_F(StorageSegmentedLogTest, truncateSuffix)
{
    log->truncateSuffix(0);
    log->truncateSuffix(10);
    EXPECT_EQ(0U, log->getLastLogIndex());
    log->appendSingle(sampleEntry);
    log->appendSingle(sampleEntry);
    log->truncateSuffix(10);
    EXPECT_EQ(2U, log->getLastLogIndex());
    log->truncateSuffix(2);
    EXPECT_EQ(2U, log->getLastLogIndex());
    log->truncateSuffix(1);
    EXPECT_EQ(1U, log->getLastLogIndex());
    log->truncateSuffix(0);
    EXPECT_EQ(0U, log->getLastLogIndex());


    log->truncatePrefix(10);
    log->appendSingle(sampleEntry);
    EXPECT_EQ(10U, log->getLastLogIndex());
    log->truncateSuffix(10);
    EXPECT_EQ(10U, log->getLastLogIndex());
    log->truncateSuffix(8);
    EXPECT_EQ(9U, log->getLastLogIndex());
    log->appendSingle(sampleEntry);
    EXPECT_EQ(10U, log->getLastLogIndex());
}

} // namespace LogCabin::Storage::<anonymous>
} // namespace LogCabin::Storage
} // namespace LogCabin
#endif
