/* Copyright (c) 2012 Stanford University
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
#include <deque>
#include <queue>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Client/Client.h"
#include "Client/ClientImpl.h"
#include "Client/LeaderRPCMock.h"
#include "Core/ProtoBuf.h"
#include "build/Protocol/Client.pb.h"

namespace LogCabin {
namespace {

std::string entryDataString(const Client::Entry& entry)
{
    return std::string(static_cast<const char*>(entry.getData()),
                       entry.getLength());
}

class ClientMockClientImplTest : public ::testing::Test {
  public:
    ClientMockClientImplTest()
        : cluster(new Client::Cluster(Client::Cluster::FOR_TESTING))
    {
    }
    std::unique_ptr<Client::Cluster> cluster;
};

TEST_F(ClientMockClientImplTest, openLog) {
    Client::Log log = cluster->openLog("testLog");
    EXPECT_EQ("testLog", log.name);
    EXPECT_EQ(0U, log.logId);
    Client::Log log2 = cluster->openLog("testLog2");
    EXPECT_EQ("testLog2", log2.name);
    EXPECT_EQ(1U, log2.logId);
}

TEST_F(ClientMockClientImplTest, deleteLog) {
    Client::Log log = cluster->openLog("testLog");
    cluster->deleteLog("testLog");
    cluster->deleteLog("testLog2");
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
}

TEST_F(ClientMockClientImplTest, listLogs) {
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
    cluster->openLog("testLog3");
    cluster->openLog("testLog1");
    cluster->openLog("testLog2");
    EXPECT_EQ((std::vector<std::string> {
               "testLog1",
               "testLog2",
               "testLog3",
              }),
              cluster->listLogs());
}

class ClientMockClientImplLogTest : public ClientMockClientImplTest {
  public:
    ClientMockClientImplLogTest()
        : log()
    {
        log.reset(new Client::Log(cluster->openLog("testLog")));
    }
    std::unique_ptr<Client::Log> log;
};

TEST_F(ClientMockClientImplLogTest, append_normal)
{
    std::vector<Client::EntryId> invalidates = {10, 20, 30};
    Client::Entry entry("hello", 5, invalidates);
    EXPECT_EQ(0U, log->append(entry));
    std::vector<Client::Entry> entries = log->read(0);
    Client::Entry& readEntry = entries.at(0);
    EXPECT_EQ(0U, readEntry.getId());
    EXPECT_EQ(invalidates, readEntry.getInvalidates());
    EXPECT_EQ("hello", entryDataString(readEntry));
}

TEST_F(ClientMockClientImplLogTest, append_expectedId)
{
    Client::Entry entry("hello", 5);
    EXPECT_EQ(0U, log->append(entry, 0));
    EXPECT_EQ(1U, log->append(entry, 1));
    EXPECT_EQ(Client::NO_ID, log->append(entry, 1));
    EXPECT_EQ(2U, log->append(entry, Client::NO_ID));
}

TEST_F(ClientMockClientImplLogTest, append_logDisappeared)
{
    Client::Entry entry("hello", 5);
    cluster->deleteLog("testLog");
    EXPECT_THROW(log->append(entry),
                 Client::LogDisappearedException);
}

TEST_F(ClientMockClientImplLogTest, read_normal)
{
    log->append(Client::Entry("hello", 5));
    log->append(Client::Entry("goodbye", 7));

    EXPECT_EQ(2U, log->read(0).size());
    EXPECT_EQ(1U, log->read(1).size());
    EXPECT_EQ(0U, log->read(2).size());
    EXPECT_EQ(0U, log->read(2000).size());

    std::vector<Client::Entry> entries = log->read(1);
    EXPECT_EQ("goodbye", entryDataString(entries.at(0)));
}

TEST_F(ClientMockClientImplLogTest, read_logDisappeared)
{
    cluster->deleteLog("testLog");
    EXPECT_THROW(log->read(0),
                 Client::LogDisappearedException);
}

TEST_F(ClientMockClientImplLogTest, getLastId_normal)
{
    EXPECT_EQ(Client::NO_ID, log->getLastId());
    log->append(Client::Entry("hello", 5));
    EXPECT_EQ(0U, log->getLastId());
}

TEST_F(ClientMockClientImplLogTest, getLastId_logDisappeared)
{
    cluster->deleteLog("testLog");
    EXPECT_THROW(log->getLastId(),
                 Client::LogDisappearedException);
}

} // namespace LogCabin::<anonymous>
} // namespace LogCabin
