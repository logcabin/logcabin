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

#include "RPC/ProtoBuf.h"
#include "build/Core/ProtoBufTest.pb.h"

namespace LogCabin {
namespace RPC {
namespace {

using LogCabin::ProtoBuf::TestMessage;

TEST(ProtoBufTest, parse) {
    RPC::Buffer rpc;
    TestMessage m;
    EXPECT_FALSE(ProtoBuf::parse(rpc, m));
    m.set_field_a(3);
    m.set_field_b(5);
    ProtoBuf::serialize(m, rpc, 8);
    *static_cast<uint64_t*>(rpc.getData()) = 0xdeadbeefdeadbeef;
    m.Clear();
    EXPECT_TRUE(ProtoBuf::parse(rpc, m, 8));
    EXPECT_EQ("field_a: 3 field_b: 5", m.ShortDebugString());
}

TEST(ProtoBufTest, serialize) {
    RPC::Buffer rpc;
    TestMessage m;
    EXPECT_DEATH(ProtoBuf::serialize(m, rpc, 3),
                 "Missing fields in protocol buffer.*: field_a, field_b");
    m.set_field_a(3);
    m.set_field_b(5);
    ProtoBuf::serialize(m, rpc, 8);
    *static_cast<uint64_t*>(rpc.getData()) = 0xdeadbeefdeadbeef;
    m.Clear();
    EXPECT_TRUE(ProtoBuf::parse(rpc, m, 8));
    EXPECT_EQ("field_a: 3 field_b: 5", m.ShortDebugString());
}


} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
