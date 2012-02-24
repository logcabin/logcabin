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

#include "include/Common.h"
#include "include/Debug.h"
#include "Client/Client.h"
#include "Client/ClientImpl.h"
#include "../build/proto/dlog.pb.h"

namespace ProtoBuf = DLog::ProtoBuf;

namespace LogCabin {

namespace {
uint32_t errorCallbackCount;
class MockErrorCallback : public Client::ErrorCallback {
  public:
    void callback() {
        ++errorCallbackCount;
    }
};

class MockRPC : public Client::PlaceholderRPC {
  public:
    typedef std::unique_ptr<google::protobuf::Message> MessagePtr;
    MockRPC()
        : requestLog()
        , responseQueue()
    {
        Client::placeholderRPC = this;
    }
    ~MockRPC() {
        Client::placeholderRPC = NULL;
    }
    void expect(OpCode opCode,
                const google::protobuf::Message& response) {
        MessagePtr responseCopy(response.New());
        responseCopy->CopyFrom(response);
        responseQueue.push({opCode, std::move(responseCopy)});
    }
    MessagePtr popRequest() {
        MessagePtr request = std::move(requestLog.at(0).second);
        requestLog.pop_front();
        return std::move(request);
    }
    void leader(OpCode opCode,
                const google::protobuf::Message& request,
                google::protobuf::Message& response) {
        MessagePtr requestCopy(request.New());
        requestCopy->CopyFrom(request);
        requestLog.push_back({opCode, std::move(requestCopy)});
        ASSERT_LT(0U, responseQueue.size())
            << "The client sent an unexpected RPC:\n"
            << request.GetTypeName() << ":\n"
            << ProtoBuf::dumpString(request, false);
        auto& opCodeMsgPair = responseQueue.front();
        EXPECT_EQ(opCode, opCodeMsgPair.first);
        response.CopyFrom(*opCodeMsgPair.second);
        responseQueue.pop();
    }
    std::deque<std::pair<OpCode, MessagePtr>> requestLog;
    std::queue<std::pair<OpCode, MessagePtr>> responseQueue;
};

class ClientClusterTest : public ::testing::Test {
  public:
    ClientClusterTest()
        : mockRPC()
        , cluster(new Client::Cluster("127.0.0.1:2106"))
    {
        errorCallbackCount = 0;
    }
    MockRPC mockRPC;
    std::unique_ptr<Client::Cluster> cluster;
};

TEST_F(ClientClusterTest, constructor) {
    // TODO(ongaro): test
}

TEST_F(ClientClusterTest, registerErrorCallback) {
    cluster->registerErrorCallback(DLog::unique<MockErrorCallback>());
    // TODO(ongaro): test
    EXPECT_EQ(0U, errorCallbackCount);
}

TEST_F(ClientClusterTest, openLog) {
    mockRPC.expect(MockRPC::OpCode::OPEN_LOG,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::OpenLog::Response>(
            "log_id: 1"));
    Client::Log log = cluster->openLog("testLog");
    EXPECT_EQ("log_name: 'testLog'",
              *mockRPC.requestLog.at(0).second);
    EXPECT_EQ("testLog", log.name);
    EXPECT_EQ(1U, log.logId);
}

TEST_F(ClientClusterTest, deleteLog) {
    mockRPC.expect(MockRPC::OpCode::DELETE_LOG,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::DeleteLog::Response>(""));
    cluster->deleteLog("testLog");
    EXPECT_EQ("log_name: 'testLog'",
              *mockRPC.popRequest());
}

TEST_F(ClientClusterTest, listLogs_none) {
    mockRPC.expect(MockRPC::OpCode::LIST_LOGS,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::ListLogs::Response>(
            ""));
    EXPECT_EQ((std::vector<std::string> {}),
              cluster->listLogs());
    EXPECT_EQ("", *mockRPC.popRequest());
}

TEST_F(ClientClusterTest, listLogs_normal) {
    mockRPC.expect(MockRPC::OpCode::LIST_LOGS,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::ListLogs::Response>(
            "log_names: ['testLog2', 'testLog3', 'testLog1']"));
    EXPECT_EQ((std::vector<std::string> {
               "testLog1",
               "testLog2",
               "testLog3",
              }),
              cluster->listLogs());
    EXPECT_EQ("", *mockRPC.popRequest());
}

class ClientLogTest : public ClientClusterTest {
  public:
    ClientLogTest()
        : log()
    {
        mockRPC.expect(MockRPC::OpCode::OPEN_LOG,
            ProtoBuf::fromString<ProtoBuf::ClientRPC::OpenLog::Response>(
                "log_id: 1"));
        log.reset(new Client::Log(cluster->openLog("testLog")));
        mockRPC.popRequest();
    }
    std::unique_ptr<Client::Log> log;
};

TEST_F(ClientLogTest, append_empty)
{
    Client::Entry entry("empty", 0);
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->append(entry));
    ProtoBuf::ClientRPC::Append::Request request;
    request.CopyFrom(*mockRPC.popRequest());
    EXPECT_EQ("log_id: 1 "
              "data: '' ",
              request);
    EXPECT_TRUE(request.has_data());
    EXPECT_EQ(0U, request.data().size());
}

TEST_F(ClientLogTest, append_justData)
{
    Client::Entry entry("hello", 5);
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->append(entry));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, append_withInvalidates)
{
    Client::Entry entry("hello", 5, { 10, 12, 14 });
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U, log->append(entry));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "invalidates: [10, 12, 14]",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, append_previousIdOk)
{
    Client::Entry entry("hello", 5);
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->append(entry, 31));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "previous_entry_id: 31",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, append_previousIdStale)
{
    Client::Entry entry("hello", 5);
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            DLog::format("ok { entry_id: %lu }", Client::NO_ID)));
    EXPECT_EQ(Client::NO_ID,
              log->append(entry, 31));
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' "
              "previous_entry_id: 31",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, append_logDisappeared)
{
    Client::Entry entry("hello", 5);
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->append(entry),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 "
              "data: 'hello' ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, invalidate_empty)
{
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->invalidate({}));
    EXPECT_EQ("log_id: 1 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, invalidate_previousIdOK)
{
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "ok { entry_id: 32 }"));
    EXPECT_EQ(32U,
              log->invalidate({1}, 31));
    EXPECT_EQ("log_id: 1 "
              "invalidates: [1] "
              "previous_entry_id: 31 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, invalidate_previousIdStale)
{
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            DLog::format("ok { entry_id: %lu }", Client::NO_ID)));
    EXPECT_EQ(Client::NO_ID,
              log->invalidate({1}, 31));
    EXPECT_EQ("log_id: 1 "
              "invalidates: [1] "
              "previous_entry_id: 31 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, invalidate_logDisappeared)
{
    mockRPC.expect(MockRPC::OpCode::APPEND,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Append::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->invalidate({1}),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 "
              "invalidates: [1] ",
              *mockRPC.popRequest());
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
    mockRPC.expect(MockRPC::OpCode::READ,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Read::Response>(
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
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, read_pastHead)
{
    mockRPC.expect(MockRPC::OpCode::READ,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Read::Response>(
            "ok {}"));
    EXPECT_EQ(0U, log->read(20).size());
    EXPECT_EQ("log_id: 1 "
              "from_entry_id: 20 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, read_logDisappeared)
{
    mockRPC.expect(MockRPC::OpCode::READ,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::Read::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->read(20),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 "
              "from_entry_id: 20 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, getLastId_emptyLog)
{
    mockRPC.expect(MockRPC::OpCode::GET_LAST_ID,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::GetLastId::Response>(
            DLog::format("ok { head_entry_id: %lu }", Client::NO_ID)));
    EXPECT_EQ(Client::NO_ID,
              log->getLastId());
    EXPECT_EQ("log_id: 1 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, getLastId_normal)
{
    mockRPC.expect(MockRPC::OpCode::GET_LAST_ID,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::GetLastId::Response>(
            "ok { head_entry_id: 20 }"));
    EXPECT_EQ(20U,
              log->getLastId());
    EXPECT_EQ("log_id: 1 ",
              *mockRPC.popRequest());
}

TEST_F(ClientLogTest, getLastId_logDisappeared)
{
    mockRPC.expect(MockRPC::OpCode::GET_LAST_ID,
        ProtoBuf::fromString<ProtoBuf::ClientRPC::GetLastId::Response>(
            "log_disappeared {}"));
    EXPECT_THROW(log->getLastId(),
                 Client::LogDisappearedException);
    EXPECT_EQ("log_id: 1 ",
              *mockRPC.popRequest());
}

} // namespace LogCabin::<anonymous>
} // namespace LogCabin
