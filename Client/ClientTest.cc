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

using Core::ProtoBuf::fromString;
using Core::StringUtil::format;

class ClientClusterTest : public ::testing::Test {
  public:
    typedef Client::LeaderRPCMock::OpCode OpCode;
    ClientClusterTest()
        : cluster(new Client::Cluster("-MOCK-SKIP-INIT-"))
        , mockRPC()
    {
        Client::ClientImpl* client =
            dynamic_cast<Client::ClientImpl*>(cluster->clientImpl.get());
        mockRPC = new Client::LeaderRPCMock();
        mockRPC->expect(OpCode::GET_SUPPORTED_RPC_VERSIONS,
            fromString<Protocol::Client::GetSupportedRPCVersions::Response>(
                        "min_version: 1, max_version: 1"));
        client->leaderRPC = std::unique_ptr<Client::LeaderRPCBase>(mockRPC);
        cluster->clientImpl->init(cluster->clientImpl, "127.0.0.1:0");
        mockRPC->popRequest();

        client->exactlyOnceRPCHelper.client = NULL;
    }
    std::unique_ptr<Client::Cluster> cluster;
    Client::LeaderRPCMock* mockRPC;
    ClientClusterTest(const ClientClusterTest&) = delete;
    ClientClusterTest& operator=(const ClientClusterTest&) = delete;
};


// Client::Cluster(FOR_TESTING) tested in MockClientImplTest.cc

TEST_F(ClientClusterTest, constructor) {
    // TODO(ongaro): test
}

TEST_F(ClientClusterTest, openLog) {
    mockRPC->expect(OpCode::OPEN_LOG,
        fromString<Protocol::Client::OpenLog::Response>(
            "log_id: 1"));
    Client::Log log = cluster->openLog("testLog");
    EXPECT_EQ("log_name: 'testLog' "
              "exactly_once {} ",
              *mockRPC->requestLog.at(0).second);
    EXPECT_EQ("testLog", log.name);
    EXPECT_EQ(1U, log.logId);
}

TEST_F(ClientClusterTest, deleteLog) {
    mockRPC->expect(OpCode::DELETE_LOG,
        fromString<Protocol::Client::DeleteLog::Response>(""));
    cluster->deleteLog("testLog");
    EXPECT_EQ("log_name: 'testLog' "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientClusterTest, listLogs_none) {
    mockRPC->expect(OpCode::LIST_LOGS,
        fromString<Protocol::Client::ListLogs::Response>(
            ""));
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
    EXPECT_EQ("", *mockRPC->popRequest());
}

TEST_F(ClientClusterTest, listLogs_normal) {
    mockRPC->expect(OpCode::LIST_LOGS,
        fromString<Protocol::Client::ListLogs::Response>(
            "log_names: ['testLog2', 'testLog3', 'testLog1']"));
    EXPECT_EQ((std::vector<std::string> {
               "testLog1",
               "testLog2",
               "testLog3",
              }),
              cluster->listLogs());
    EXPECT_EQ("", *mockRPC->popRequest());
}

// TODO(ongaro): test getConfiguration, setConfiguraton

class ClientLogTest : public ClientClusterTest {
  public:
    ClientLogTest()
        : log()
    {
        mockRPC->expect(OpCode::OPEN_LOG,
            fromString<Protocol::Client::OpenLog::Response>(
                "log_id: 1"));
        log.reset(new Client::Log(cluster->openLog("testLog")));
        mockRPC->popRequest();
    }
    std::unique_ptr<Client::Log> log;
};

TEST_F(ClientLogTest, append_empty)
{
    Client::Entry entry("empty", 0);
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->append(entry));
    Protocol::Client::Append::Request request;
    request.CopyFrom(*mockRPC->popRequest());
    EXPECT_EQ("log_id: 1 "
              "data: '' "
              "exactly_once {} ",
              request);
    EXPECT_TRUE(request.has_data());
    EXPECT_EQ(0U, request.data().size());
}

TEST_F(ClientLogTest, append_justData)
{
    Client::Entry entry("hello", 5);
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->append(entry));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, append_withInvalidates)
{
    Client::Entry entry("hello", 5, { 10, 12, 14 });
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U, log->append(entry));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "invalidates: [10, 12, 14] "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, append_expectedIdOk)
{
    Client::Entry entry("hello", 5);
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->append(entry, 32));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "expected_entry_id: 32 "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, append_expectedIdStale)
{
    Client::Entry entry("hello", 5);
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            format("ok { entry_id: %lu }", Client::NO_ID)));
    EXPECT_EQ(Client::NO_ID,
              log->append(entry, 32));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "expected_entry_id: 32 "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, append_logDisappeared)
{
    Client::Entry entry("hello", 5);
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->append(entry),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, invalidate_empty)
{
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->invalidate({}));
    EXPECT_EQ("log_id: 1 "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, invalidate_expectedIdOK)
{
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->invalidate({1}, 32));
    EXPECT_EQ("log_id: 1 "
              "invalidates: [1] "
              "expected_entry_id: 32 "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, invalidate_expectedIdStale)
{
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            format("ok { entry_id: %lu }", Client::NO_ID)));
    EXPECT_EQ(Client::NO_ID,
              log->invalidate({1}, 32));
    EXPECT_EQ("log_id: 1 "
              "invalidates: [1] "
              "expected_entry_id: 32 "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, invalidate_logDisappeared)
{
    mockRPC->expect(OpCode::APPEND,
        fromString<Protocol::Client::Append::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->invalidate({1}),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 "
              "invalidates: [1] "
              "exactly_once {} ",
              *mockRPC->popRequest());
}

namespace {
std::string entryDataString(const Client::Entry& entry)
{
    return std::string(static_cast<const char*>(entry.getData()),
                       entry.getLength());
}
} // anonymous namespace

TEST_F(ClientLogTest, read_normal)
{
    mockRPC->expect(OpCode::READ,
        fromString<Protocol::Client::Read::Response>(
            "ok { "
            "   entry: { entry_id: 20 } "
            "   entry: { entry_id: 22, invalidates: [12, 14] } "
            "   entry: { entry_id: 24, data: 'hello' } "
            "   entry: { entry_id: 26, invalidates: [16, 18], data: 'bye' } "
            "   entry: { entry_id: 28, data: '' } "
            "}"));
    std::vector<Client::Entry> entries = log->read(20);
    ASSERT_EQ(5U, entries.size());
    EXPECT_EQ(20U, entries[0].getId());
    EXPECT_EQ(22U, entries[1].getId());
    EXPECT_EQ(24U, entries[2].getId());
    EXPECT_EQ(26U, entries[3].getId());
    EXPECT_EQ(28U, entries[4].getId());
    EXPECT_TRUE(NULL == entries[0].getData());
    EXPECT_EQ(0U, entries[0].getLength());
    EXPECT_TRUE(NULL == entries[1].getData());
    EXPECT_EQ(0U, entries[1].getLength());
    EXPECT_EQ("hello", entryDataString(entries[2]));
    EXPECT_EQ("bye", entryDataString(entries[3]));
    EXPECT_EQ("", entryDataString(entries[4]));
    EXPECT_EQ((std::vector<Client::EntryId>{}),
              entries[0].getInvalidates());
    EXPECT_EQ((std::vector<Client::EntryId>{ 12, 14 }),
              entries[1].getInvalidates());
    EXPECT_EQ((std::vector<Client::EntryId>{}),
              entries[2].getInvalidates());
    EXPECT_EQ((std::vector<Client::EntryId>{ 16, 18 }),
              entries[3].getInvalidates());
    EXPECT_EQ((std::vector<Client::EntryId>{}),
              entries[4].getInvalidates());
    EXPECT_EQ("log_id: 1 "
              "from_entry_id: 20 ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, read_pastHead)
{
    mockRPC->expect(OpCode::READ,
        fromString<Protocol::Client::Read::Response>(
            "ok {}"));
    EXPECT_EQ(0U, log->read(20).size());
    EXPECT_EQ("log_id: 1 "
              "from_entry_id: 20 ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, read_logDisappeared)
{
    mockRPC->expect(OpCode::READ,
        fromString<Protocol::Client::Read::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->read(20),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 "
              "from_entry_id: 20 ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, getLastId_emptyLog)
{
    mockRPC->expect(OpCode::GET_LAST_ID,
        fromString<Protocol::Client::GetLastId::Response>(
            format("ok { head_entry_id: %lu }", Client::NO_ID)));
    EXPECT_EQ(Client::NO_ID,
              log->getLastId());
    EXPECT_EQ("log_id: 1 ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, getLastId_normal)
{
    mockRPC->expect(OpCode::GET_LAST_ID,
        fromString<Protocol::Client::GetLastId::Response>(
            "ok { head_entry_id: 20 }"));
    EXPECT_EQ(20U,
              log->getLastId());
    EXPECT_EQ("log_id: 1 ",
              *mockRPC->popRequest());
}

TEST_F(ClientLogTest, getLastId_logDisappeared)
{
    mockRPC->expect(OpCode::GET_LAST_ID,
        fromString<Protocol::Client::GetLastId::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->getLastId(),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 ",
              *mockRPC->popRequest());
}

// This is to test the serialization/deserialization of Tree RPCs in both the
// client library and Tree/ProtoBuf.h.
class ClientTreeTest : public ::testing::Test {
  public:
    ClientTreeTest()
        : cluster(new Client::Cluster(Client::Cluster::FOR_TESTING))
    {
    }
    std::unique_ptr<Client::Cluster> cluster;
    ClientTreeTest(const ClientTreeTest&) = delete;
    ClientTreeTest& operator=(const ClientTreeTest&) = delete;
};

using Client::Result;
using Client::Status;

#define EXPECT_OK(c) do { \
    Result result = (c); \
    EXPECT_EQ(Status::OK, result.status) << result.error; \
} while (0)

TEST_F(ClientTreeTest, makeDirectory)
{
    EXPECT_OK(cluster->makeDirectory("/foo"));
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              cluster->makeDirectory("").status);
    EXPECT_FALSE(cluster->makeDirectory("").error.empty());
    std::vector<std::string> children;
    EXPECT_OK(cluster->listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{"foo/"}),
              children);
}

TEST_F(ClientTreeTest, listDirectory)
{
    std::vector<std::string> children;
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              cluster->listDirectory("", children).status);
    EXPECT_FALSE(cluster->listDirectory("", children).error.empty());
    EXPECT_OK(cluster->listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{}),
              children);
    EXPECT_OK(cluster->makeDirectory("/foo"));
    EXPECT_OK(cluster->listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{"foo/"}),
              children);
}

TEST_F(ClientTreeTest, removeDirectory)
{
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              cluster->removeDirectory("").status);
    EXPECT_OK(cluster->makeDirectory("/foo"));
    EXPECT_OK(cluster->removeDirectory("/foo"));
    std::vector<std::string> children;
    EXPECT_OK(cluster->listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{}),
              children);
}

TEST_F(ClientTreeTest, write)
{
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              cluster->write("", "bar").status);
    EXPECT_OK(cluster->write("/foo", "bar"));
    std::string contents;
    EXPECT_OK(cluster->read("/foo", contents));
    EXPECT_EQ("bar", contents);
}

TEST_F(ClientTreeTest, read)
{
    std::string contents;
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              cluster->read("", contents).status);
    EXPECT_OK(cluster->write("/foo", "bar"));
    EXPECT_OK(cluster->read("/foo", contents));
    EXPECT_EQ("bar", contents);
}

TEST_F(ClientTreeTest, removeFile)
{
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              cluster->removeFile("").status);
    EXPECT_OK(cluster->write("/foo", "bar"));
    EXPECT_OK(cluster->removeFile("/foo"));
    std::vector<std::string> children;
    EXPECT_OK(cluster->listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{}),
              children);
}

} // namespace LogCabin::<anonymous>
} // namespace LogCabin
