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
#include <stdexcept>
#include "Server/RaftLog.h"

namespace LogCabin {
namespace Server {
namespace {

using namespace RaftConsensusInternal; // NOLINT

class ServerRaftLogTest : public ::testing::Test {
    ServerRaftLogTest()
        : log()
        , sampleEntry()
    {
        sampleEntry.set_term(40);
        sampleEntry.set_data("foo");
    }
    Log log;
    Log::Entry sampleEntry;
};

TEST_F(ServerRaftLogTest, basic)
{
    EXPECT_EQ(1U, log.append(sampleEntry));
    Log::Entry entry = log.getEntry(1);
    EXPECT_EQ(40U, entry.term());
    EXPECT_EQ("foo", entry.data());
}

TEST_F(ServerRaftLogTest, getEntry)
{
    Log::Entry entry = log.getEntry(log.append(sampleEntry));
    EXPECT_EQ(40U, entry.term());
    EXPECT_EQ("foo", entry.data());
    EXPECT_THROW(log.getEntry(0), std::out_of_range);
    EXPECT_THROW(log.getEntry(2), std::out_of_range);
}

TEST_F(ServerRaftLogTest, getLastLogId)
{
    EXPECT_EQ(0U, log.getLastLogId());
    log.append(sampleEntry);
    log.append(sampleEntry);
    EXPECT_EQ(2U, log.getLastLogId());
}

TEST_F(ServerRaftLogTest, getTerm)
{
    EXPECT_EQ(0U, log.getTerm(0));
    EXPECT_EQ(0U, log.getTerm(1));
    EXPECT_EQ(0U, log.getTerm(1000));
    log.append(sampleEntry);
    EXPECT_EQ(40U, log.getTerm(1));
}


TEST_F(ServerRaftLogTest, truncate)
{
    log.truncate(0);
    log.truncate(10);
    EXPECT_EQ(0U, log.getLastLogId());
    log.append(sampleEntry);
    log.append(sampleEntry);
    log.truncate(10);
    EXPECT_EQ(2U, log.getLastLogId());
    log.truncate(2);
    EXPECT_EQ(2U, log.getLastLogId());
    log.truncate(1);
    EXPECT_EQ(1U, log.getLastLogId());
    log.truncate(0);
    EXPECT_EQ(0U, log.getLastLogId());
}

#if 0
TEST_F(ServerRaftLogTest, init)
{
    Log::Entry c;
    c.term = 0;
    c.type = Protocol::Raft::EntryType::CONFIGURATION;
    auto* s = c.configuration.mutable_prev_configuration()->add_servers();
    s->set_server_id(1);
    s->set_address("localhost:61023");
    log.path = "/tmp/c";
    log.append(c);
    log.read("/tmp/c/00000001");
}
#endif

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
