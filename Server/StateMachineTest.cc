/* Copyright (c) 2013-2014 Stanford University
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
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "build/Protocol/Raft.pb.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "Core/STLUtil.h"
#include "Server/Globals.h"
#include "Server/RaftConsensus.h"
#include "Server/StateMachine.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/MemoryLog.h"
#include "Storage/SnapshotFile.h"
#include "include/LogCabin/Debug.h"

namespace LogCabin {
namespace Server {
extern bool stateMachineSuppressThreads;
extern uint32_t stateMachineChildSleepMs;
namespace {

class ServerStateMachineTest : public ::testing::Test {
  public:
    ServerStateMachineTest()
      : globals()
      , consensus()
      , stateMachine()
    {
        RaftConsensusInternal::startThreads = false;
        consensus.reset(new RaftConsensus(globals));
        consensus->serverId = 1;
        consensus->log.reset(new Storage::MemoryLog());
        consensus->storageLayout.initTemporary();

        Storage::Log::Entry entry;
        entry.set_term(1);
        entry.set_type(Protocol::Raft::EntryType::CONFIGURATION);
        *entry.mutable_configuration() =
            Core::ProtoBuf::fromString<Protocol::Raft::Configuration>(
                "prev_configuration {"
                "    servers { server_id: 1, address: '127.0.0.1:61023' }"
                "}");
        consensus->init();
        consensus->append({&entry});
        consensus->startNewElection();
        consensus->configuration->localServer->lastSyncedIndex =
            consensus->log->getLastLogIndex();
        consensus->advanceCommitIndex();

        stateMachineSuppressThreads = true;
        stateMachine.reset(new StateMachine(consensus, globals.config));
    }
    ~ServerStateMachineTest() {
        stateMachineSuppressThreads = false;
        stateMachineChildSleepMs = 0;
    }


    Core::Buffer
    serialize(const Protocol::Client::Command& command) {
        Core::Buffer out;
        Core::ProtoBuf::serialize(command, out);
        return out;
    }

    Globals globals;
    std::shared_ptr<RaftConsensus> consensus;
    std::unique_ptr<StateMachine> stateMachine;
};

TEST_F(ServerStateMachineTest, getResponse)
{
    Core::Debug::setLogPolicy({{"Server/StateMachine.cc", "ERROR"}});
    stateMachine->sessions.insert({1, {}});
    StateMachine::Session& session = stateMachine->sessions.at(1);
    Protocol::Client::CommandResponse r1;
    Protocol::Client::CommandResponse r2;
    r1.mutable_tree()->set_status(Protocol::Client::Status::LOOKUP_ERROR);
    session.responses.insert({1, r1});
    Protocol::Client::ExactlyOnceRPCInfo rpcInfo;
    rpcInfo.set_client_id(2);
    rpcInfo.set_rpc_number(1);
    EXPECT_FALSE(stateMachine->getResponse(rpcInfo, r2));
    rpcInfo.set_client_id(1);
    rpcInfo.set_rpc_number(2);
    EXPECT_FALSE(stateMachine->getResponse(rpcInfo, r2));
    Core::Debug::setLogPolicy({{"", "WARNING"}});
    rpcInfo.set_client_id(1);
    rpcInfo.set_rpc_number(1);
    EXPECT_TRUE(stateMachine->getResponse(rpcInfo, r2));
    EXPECT_EQ(r1, r2);
}

TEST_F(ServerStateMachineTest, apply_tree)
{
    stateMachine->sessionTimeoutNanos = 1;
    RaftConsensus::Entry entry;
    entry.index = 6;
    entry.type = RaftConsensus::Entry::DATA;
    entry.clusterTime = 2;
    Protocol::Client::Command command =
        Core::ProtoBuf::fromString<Protocol::Client::Command>(
            "tree: { "
            " exactly_once: { "
            "  client_id: 39 "
            "  first_outstanding_rpc: 2 "
            "  rpc_number: 3 "
            " } "
            " make_directory { "
            "  path: '/a' "
            " } "
            "}");
    entry.command = serialize(command);
    std::vector<std::string> children;

    // session does not exist
    stateMachine->sessions.insert({1, {}});
    stateMachine->apply(entry);
    stateMachine->expireSessions(entry.clusterTime);
    stateMachine->tree.listDirectory("/", children);
    EXPECT_EQ((std::vector<std::string> {}), children);
    ASSERT_EQ(0U, stateMachine->sessions.size());

    // session exists and need to apply
    stateMachine->sessions.insert({1, {}});
    stateMachine->sessions.insert({39, {}});
    stateMachine->apply(entry);
    stateMachine->expireSessions(entry.clusterTime);
    stateMachine->tree.listDirectory("/", children);
    EXPECT_EQ((std::vector<std::string> {"a/"}), children);
    ASSERT_EQ(1U, stateMachine->sessions.size());
    EXPECT_EQ(2U, stateMachine->sessions.at(39).lastModified);

    // session exists and response exists
    stateMachine->sessions.insert({1, {}});
    stateMachine->tree.removeDirectory("/a");
    stateMachine->apply(entry);
    stateMachine->expireSessions(entry.clusterTime);
    stateMachine->tree.listDirectory("/", children);
    EXPECT_EQ((std::vector<std::string> {}), children);
    ASSERT_EQ(1U, stateMachine->sessions.size());
    EXPECT_EQ(2U, stateMachine->sessions.at(39).lastModified);

    // session exists but response discarded
    stateMachine->sessions.insert({1, {}});
    stateMachine->expireResponses(stateMachine->sessions.at(39), 4);
    stateMachine->apply(entry);
    stateMachine->expireSessions(entry.clusterTime);
    stateMachine->tree.listDirectory("/", children);
    EXPECT_EQ((std::vector<std::string> {}), children);
    ASSERT_EQ(1U, stateMachine->sessions.size());
    EXPECT_EQ(2U, stateMachine->sessions.at(39).lastModified);
}

TEST_F(ServerStateMachineTest, apply_openSession)
{
    stateMachine->sessionTimeoutNanos = 1;
    stateMachine->sessions.insert({1, {}});
    Protocol::Client::Command command =
        Core::ProtoBuf::fromString<Protocol::Client::Command>(
            "open_session: {}");
    RaftConsensus::Entry entry;
    entry.index = 6;
    entry.type = RaftConsensus::Entry::DATA;
    entry.command = serialize(command);
    entry.clusterTime = 2;

    stateMachine->apply(entry);
    stateMachine->expireSessions(entry.clusterTime);
    ASSERT_EQ((std::vector<uint64_t>{6U}),
              Core::STLUtil::sorted(
                  Core::STLUtil::getKeys(stateMachine->sessions)));
    StateMachine::Session& session = stateMachine->sessions.at(6);
    EXPECT_EQ(2U, session.lastModified);
    EXPECT_EQ(0U, session.firstOutstandingRPC);
    EXPECT_EQ(0U, session.responses.size());
}

// This tries to test the use of kill() to stop a snapshotting child and exit
// quickly.
TEST_F(ServerStateMachineTest, applyThreadMain_exiting_TimingSensitive)
{
    // instruct the child process to sleep for 10s
    stateMachineChildSleepMs = 10000;
    consensus->exit();
    {
        // applyThread won't be able to kill() yet due to mutex
        std::unique_lock<std::mutex> lockGuard(stateMachine->mutex);
        stateMachine->applyThread = std::thread(&StateMachine::applyThreadMain,
                                                stateMachine.get());
        struct timeval startTime;
        EXPECT_EQ(0, gettimeofday(&startTime, NULL));
        stateMachine->takeSnapshot(1, lockGuard);
        struct timeval endTime;
        EXPECT_EQ(0, gettimeofday(&endTime, NULL));
        uint64_t elapsedMillis =
            ((endTime.tv_sec   * 1000 * 1000 + endTime.tv_usec) -
             (startTime.tv_sec * 1000 * 1000 + startTime.tv_usec)) / 1000;
        EXPECT_GT(200U, elapsedMillis) <<
            "This test depends on timing, so failures are likely under "
            "heavy load, valgrind, etc.";
    }
    EXPECT_EQ(0U, consensus->lastSnapshotIndex);
}

TEST_F(ServerStateMachineTest, dumpSessionSnapshot)
{
    Protocol::Client::CommandResponse r1;
    r1.mutable_tree()->set_status(Protocol::Client::Status::LOOKUP_ERROR);

    Protocol::Client::CommandResponse r2;
    r1.mutable_tree()->set_status(Protocol::Client::Status::TYPE_ERROR);

    StateMachine::Session s1;
    s1.lastModified = 6;
    s1.firstOutstandingRPC = 5;
    s1.responses.insert({5, r1});
    s1.responses.insert({7, r2});
    stateMachine->sessions.insert({4, s1});

    StateMachine::Session s2;
    s2.firstOutstandingRPC = 9;
    s2.responses.insert({10, r2});
    s2.responses.insert({11, r1});
    stateMachine->sessions.insert({80, s2});

    StateMachine::Session s3;
    s3.firstOutstandingRPC = 6;
    stateMachine->sessions.insert({91, s3});

    {
        Storage::SnapshotFile::Writer writer(consensus->storageLayout);
        stateMachine->dumpSessionSnapshot(writer.getStream());
        writer.save();
    }

    stateMachine->sessions.at(80).responses.at(10) = r1;
    stateMachine->sessions.at(80).firstOutstandingRPC = 10;

    {
        Storage::SnapshotFile::Reader reader(consensus->storageLayout);
        stateMachine->loadSessionSnapshot(reader.getStream());
    }
    EXPECT_EQ((std::vector<std::uint64_t>{4, 80, 91}),
              Core::STLUtil::sorted(
                Core::STLUtil::getKeys(stateMachine->sessions)));
    EXPECT_EQ(6U, stateMachine->sessions.at(4).lastModified);
    EXPECT_EQ(5U, stateMachine->sessions.at(4).firstOutstandingRPC);
    EXPECT_EQ(9U, stateMachine->sessions.at(80).firstOutstandingRPC);
    EXPECT_EQ(6U, stateMachine->sessions.at(91).firstOutstandingRPC);
    EXPECT_EQ((std::vector<std::uint64_t>{5, 7}),
              Core::STLUtil::sorted(
                Core::STLUtil::getKeys(
                    stateMachine->sessions.at(4).responses)));
    EXPECT_EQ(r1, stateMachine->sessions.at(4).responses.at(5));
    EXPECT_EQ(r2, stateMachine->sessions.at(4).responses.at(7));
    EXPECT_EQ((std::vector<std::uint64_t>{10, 11}),
              Core::STLUtil::sorted(
                Core::STLUtil::getKeys(
                    stateMachine->sessions.at(80).responses)));
    EXPECT_EQ(r2, stateMachine->sessions.at(80).responses.at(10));
    EXPECT_EQ(r1, stateMachine->sessions.at(80).responses.at(11));
    EXPECT_EQ((std::vector<std::uint64_t>{}),
              Core::STLUtil::sorted(
                Core::STLUtil::getKeys(
                    stateMachine->sessions.at(91).responses)));
}

TEST_F(ServerStateMachineTest, expireResponses)
{
    stateMachine->sessions.insert({1, {}});
    StateMachine::Session& session = stateMachine->sessions.at(1);
    session.responses.insert({1, {}});
    session.responses.insert({2, {}});
    session.responses.insert({4, {}});
    session.responses.insert({5, {}});
    stateMachine->expireResponses(session, 4);
    stateMachine->expireResponses(session, 3);
    EXPECT_EQ(4U, session.firstOutstandingRPC);
    EXPECT_EQ((std::vector<uint64_t>{4U, 5U}),
              Core::STLUtil::sorted(
                  Core::STLUtil::getKeys(session.responses)));
}

TEST_F(ServerStateMachineTest, expireSessions)
{
    stateMachine->sessionTimeoutNanos = 1;
    stateMachine->sessions.insert({1, {}});
    stateMachine->sessions.at(1).lastModified = 100;
    stateMachine->sessions.insert({2, {}});
    stateMachine->sessions.at(2).lastModified = 400;
    stateMachine->sessions.insert({3, {}});
    stateMachine->sessions.at(3).lastModified = 200;
    stateMachine->sessions.insert({4, {}});
    stateMachine->sessions.at(4).lastModified = 201;
    stateMachine->sessions.insert({5, {}});
    stateMachine->expireSessions(202);
    EXPECT_EQ((std::vector<uint64_t>{2U, 4U}),
              Core::STLUtil::sorted(
                  Core::STLUtil::getKeys(stateMachine->sessions)));
}

// loadSessionSnapshot tested along with dumpSessionSnapshot above

TEST_F(ServerStateMachineTest, takeSnapshot)
{
    EXPECT_EQ(0U, consensus->lastSnapshotIndex);
    stateMachine->tree.makeDirectory("/foo");
    stateMachine->sessions.insert({4, {}});
    {
        std::unique_lock<std::mutex> lockGuard(stateMachine->mutex);
        stateMachine->takeSnapshot(1, lockGuard);
    }
    stateMachine->tree.removeDirectory("/foo");
    stateMachine->sessions.clear();
    EXPECT_EQ(1U, consensus->lastSnapshotIndex);
    consensus->discardUnneededEntries();
    consensus->readSnapshot();
    stateMachine->loadSessionSnapshot(consensus->snapshotReader->getStream());
    stateMachine->tree.loadSnapshot(consensus->snapshotReader->getStream());
    std::vector<std::string> children;
    stateMachine->tree.listDirectory("/", children);
    EXPECT_EQ((std::vector<std::string>{"foo/"}), children);
    EXPECT_EQ((std::vector<std::uint64_t>{4}),
              Core::STLUtil::sorted(
                  Core::STLUtil::getKeys(stateMachine->sessions)));
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin

