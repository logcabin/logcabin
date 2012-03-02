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

#include <gtest/gtest.h>
#include <vector>

#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Storage {
namespace {

char hello[] = "hello";
char nl[] = "\n";

TEST(StorageLogEntryTest, constructorWithData) {
    RPC::Buffer data(hello, 6, NULL);
    LogEntry entry(3, std::move(data), {4, 5, 6});
    EXPECT_EQ(NO_LOG_ID, entry.logId);
    EXPECT_EQ(NO_ENTRY_ID, entry.entryId);
    EXPECT_EQ(3U, entry.createTime);
    EXPECT_EQ((std::vector<EntryId>{4, 5, 6}),
              entry.invalidations);
    EXPECT_TRUE(entry.hasData);
    EXPECT_STREQ("hello",
                 static_cast<const char*>(entry.data.getData()));
}

TEST(StorageLogEntryTest, constructorWithoutData) {
    LogEntry entry(3, {4, 5, 6});
    EXPECT_EQ(NO_LOG_ID, entry.logId);
    EXPECT_EQ(NO_ENTRY_ID, entry.entryId);
    EXPECT_EQ(3U, entry.createTime);
    EXPECT_EQ((std::vector<EntryId>{4, 5, 6}),
              entry.invalidations);
    EXPECT_FALSE(entry.hasData);
}

// move constructor and move assignment are trivial

TEST(StorageLogEntryTest, toString) {
    LogEntry entry1(3, RPC::Buffer(hello, 6, NULL), {4, 5, 6});
    LogEntry entry2(3, {4, 5, 6});
    LogEntry entry3(3, RPC::Buffer(hello, 6, NULL));
    LogEntry entry4(3, RPC::Buffer(hello, 0, NULL));
    LogEntry entry5(3, RPC::Buffer(nl, 1, NULL));
    entry1.logId = 1;
    entry1.entryId = 2;
    entry2.logId = 1;
    entry2.entryId = 2;
    entry3.logId = 1;
    entry3.entryId = 2;
    entry4.logId = 1;
    entry4.entryId = 2;
    entry5.logId = 1;
    entry5.entryId = 2;
    EXPECT_EQ("(1, 2) 'hello' [inv 4, 5, 6]", entry1.toString());
    EXPECT_EQ("(1, 2) NODATA [inv 4, 5, 6]", entry2.toString());
    EXPECT_EQ("(1, 2) 'hello'", entry3.toString());
    EXPECT_EQ("(1, 2) BINARY", entry4.toString());
    EXPECT_EQ("(1, 2) BINARY", entry5.toString());
}

} // namespace LogCabin::Storage::<anonymous>
} // namespace LogCabin::Storage
} // namespace LogCabin
