/* Copyright (c) 2011 Stanford University
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

#include <string>
#include <gtest/gtest.h>

#include "Ref.h"

namespace DLog {

namespace {

uint32_t liveCount;

class TObj {
  public:
    explicit TObj(const std::string& name)
        : name(name)
        , refCount(0)
    {
        ++liveCount;
    }
    ~TObj() {
        --liveCount;
    }
    const std::string name;
    uint32_t refCount;
};

} // anonymous namespace

TEST(RefTest, basic) {
    liveCount = 0;

    Ref<TObj> r1(*new TObj("foo"));
    EXPECT_EQ(1, r1->refCount);
    {
        Ref<TObj> r2(r1);
        EXPECT_EQ(2, r1->refCount);
    }
    EXPECT_EQ(1, r1->refCount);
    Ref<TObj> r3(*new TObj("bar"));
    r3 = r1;
    EXPECT_EQ(2, r1->refCount);

    EXPECT_EQ(1, liveCount);

    EXPECT_EQ("foo", r3->name);
    EXPECT_EQ("foo", (*r3).name);
}

TEST(PtrTest, basic) {
    liveCount = 0;

    Ref<TObj> r1(*new TObj("foo"));
    Ptr<TObj> p1(r1);
    Ptr<TObj> p2(NULL);
    Ptr<TObj> p3(new TObj("bar"));
    Ptr<TObj> p4(p1);
    Ptr<TObj> p5;

    EXPECT_FALSE(p2);
    EXPECT_FALSE(p5);

    EXPECT_EQ(3, p1->refCount);
    {
        Ptr<TObj> p5(p1);
        EXPECT_EQ(4, p1->refCount);
    }
    EXPECT_EQ(3, p1->refCount);
    EXPECT_EQ(1, p3->refCount);

    EXPECT_EQ(2, liveCount);

    EXPECT_EQ("foo", p4->name);
    EXPECT_EQ("foo", (*p4).name);
    EXPECT_EQ("bar", p3->name);
}

} // namespace DLog
