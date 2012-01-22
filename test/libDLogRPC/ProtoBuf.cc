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

#include "ProtoBuf.h"
#include "ProtoBuf.pb.h"

namespace DLog {
namespace ProtoBuf {

TEST(ProtoBufTest, equality) {
    TestMessage a;
    TestMessage b;
    EXPECT_EQ(a, a);
    EXPECT_EQ(a, b);
    b.set_field_a(3);
    EXPECT_NE(a, b);
    EXPECT_NE(b, a);
}

TEST(ProtoBufTest, equalityStr) {
    // The protobuf ERRORs during this test are normal.
    TestMessage m;
    EXPECT_EQ(m, "");
    EXPECT_EQ("", m);
    m.set_field_a(3);
    EXPECT_NE(m, "");
    EXPECT_NE("", m);
    EXPECT_EQ(m, "field_a: 3");
    EXPECT_EQ("field_a: 3", m);
}

TEST(ProtoBufTest, fromString) {
    TestMessage m;
    m = fromString<TestMessage>("field_a: 3, field_b: 5");
    EXPECT_EQ("field_a: 3 field_b: 5", m.ShortDebugString());

    EXPECT_THROW(fromString<TestMessage>(""),
                 std::runtime_error);
}

TEST(ProtoBufTest, dumpString) {
    TestMessage m;
    m.set_field_a(3);
    m.set_field_b(5);
    m.add_field_c(12);
    m.add_field_c(19);
    m.set_field_d("apostr'phe bin\01\02ry");
    // Don't really care about the exact output, but it should be printable.
    EXPECT_EQ("field_a: 3\n"
              "field_b: 5\n"
              "field_c: [12, 19]\n"
              "field_d: \"apostr\\'phe bin\\001\\002ry\"\n",
              dumpString(m, false));
    EXPECT_EQ("              \"field_a: 3\"\n"
              "              \"field_b: 5\"\n"
              "              \"field_c: [12, 19]\"\n"
              "              \"field_d: 'apostr\\'phe bin\\001\\002ry'\"\n",
              dumpString(m, true));
}

} // namespace DLog::ProtoBuf
} // namespace DLog
