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

#include "Common.h"
#include "DLogClient.h"
#include "Debug.h"

using namespace DLog;

namespace {
uint32_t errorCallbackCount;
class MockErrorCallback : public Client::ErrorCallback {
  public:
    void callback() {
        ++errorCallbackCount;
    }
};
} // anonymous namespace

class ClientClusterTest : public ::testing::Test {
  public:
    ClientClusterTest()
        : cluster(new Client::Cluster("127.0.0.1:2106"))
    {
        errorCallbackCount = 0;
    }
    std::unique_ptr<Client::Cluster> cluster;
};

TEST_F(ClientClusterTest, constructor) {
    // TODO(ongaro): test
}

TEST_F(ClientClusterTest, registerErrorCallback) {
    cluster->registerErrorCallback(unique<MockErrorCallback>());
    // TODO(ongaro): test
    EXPECT_EQ(0U, errorCallbackCount);
}

TEST_F(ClientClusterTest, openLog) {
    Client::Log log = cluster->openLog("testLog");
    EXPECT_EQ("testLog", log.name);

    Client::Log log2 = cluster->openLog("testLog");
    EXPECT_EQ(log.name, log2.name);
    EXPECT_EQ(log.logId, log2.logId);
}

TEST_F(ClientClusterTest, deleteLog) {
    cluster->deleteLog("testLog");
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
    cluster->openLog("testLog");
    cluster->deleteLog("testLog");
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
}

TEST_F(ClientClusterTest, listLogs) {
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
    cluster->openLog("testLog2");
    cluster->openLog("testLog1");
    cluster->openLog("testLog3");
    // TODO(ongaro): test
    EXPECT_EQ((std::vector<std::string> {
#if 0
               "testLog1",
               "testLog2",
               "testLog3",
#endif
              }),
              cluster->listLogs());
}

class ClientLogTest : public ClientClusterTest {
  public:
    ClientLogTest()
        : log(cluster->openLog("testLog"))
    {
    }
    Client::Log log;
};

TEST_F(ClientLogTest, append)
{
    // TODO(ongaro): test
}

TEST_F(ClientLogTest, invalidate)
{
    // TODO(ongaro): test
}

TEST_F(ClientLogTest, read)
{
    // TODO(ongaro): test
}

TEST_F(ClientLogTest, getLastId)
{
    // TODO(ongaro): test
}
