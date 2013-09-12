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

class ClientMockClientImplTest : public ::testing::Test {
  public:
    ClientMockClientImplTest()
        : cluster(new Client::Cluster(Client::Cluster::FOR_TESTING))
    {
    }
    std::unique_ptr<Client::Cluster> cluster;
};

// sanity check for tree operations (read-only and read-write)
TEST_F(ClientMockClientImplTest, tree) {
    Client::Tree tree = cluster->getTree();
    EXPECT_EQ(Client::Status::OK,
              tree.makeDirectory("/foo").status);
    std::vector<std::string> children;
    EXPECT_EQ(Client::Status::OK,
              tree.listDirectory("/", children).status);
    EXPECT_EQ((std::vector<std::string> {"foo/"}),
              children);
}

} // namespace LogCabin::<anonymous>
} // namespace LogCabin
