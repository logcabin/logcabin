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

class TObj {
  private:
    explicit TObj(const std::string& name)
        : refCount()
        , name(name)
    {
        ++liveCount;
    }
    friend class DLog::MakeHelper; // construct with make()
  public:
    ~TObj() {
        --liveCount;
    }
    RefHelper<TObj>::RefCount refCount;
    const std::string name;
    static uint32_t liveCount;
};
uint32_t TObj::liveCount;

} // anonymous namespace

TEST(RefTest, basic) {
    TObj::liveCount = 0;

    Ref<TObj> r1 = make<TObj>("foo");
    EXPECT_EQ(1U, r1->refCount.get());
    {
        Ref<TObj> r2(r1);
        EXPECT_EQ(2U, r1->refCount.get());
    }
    EXPECT_EQ(1U, r1->refCount.get());
    Ref<TObj> r3 = make<TObj>("bar");
    r3 = r1;
    EXPECT_EQ(2U, r1->refCount.get());

    EXPECT_EQ(1U, TObj::liveCount);

    EXPECT_EQ("foo", r3->name);
    EXPECT_EQ("foo", (*r3).name);
}

TEST(RefTest, selfAssignment) {
    Ref<TObj> r1 = make<TObj>("foo");
    r1 = r1;
    r1 = r1;
    EXPECT_EQ(1U, r1->refCount.get());
}

TEST(RefTest, equality) {
    Ref<TObj> r1 = make<TObj>("foo");
    Ref<TObj> r2 = make<TObj>("foo");
    Ref<TObj> r3(r1);
    EXPECT_NE(r1, r2);
    EXPECT_EQ(r1, r3);
}

TEST(PtrTest, basic) {
    TObj::liveCount = 0;

    Ref<TObj> r1 = make<TObj>("foo");
    Ptr<TObj> p1(r1);
    Ptr<TObj> p2(NULL);
    Ptr<TObj> p3 = make<TObj>("bar");
    Ptr<TObj> p4(p1);
    Ptr<TObj> p5;

    EXPECT_FALSE(p2);
    EXPECT_FALSE(p5);

    EXPECT_EQ(3U, p1->refCount.get());
    {
        Ptr<TObj> p5(p1);
        EXPECT_EQ(4U, p1->refCount.get());
    }
    EXPECT_EQ(3U, p1->refCount.get());
    EXPECT_EQ(1U, p3->refCount.get());

    EXPECT_EQ(2U, TObj::liveCount);

    EXPECT_EQ("foo", p4->name);
    EXPECT_EQ("foo", (*p4).name);
    EXPECT_EQ("bar", p3->name);
}

TEST(PtrTest, selfAssignment) {
    Ptr<TObj> p1 = make<TObj>("foo");
    p1 = p1;
    p1 = p1;
    EXPECT_EQ(1U, p1->refCount.get());
}

TEST(PtrTest, equality) {
    Ref<TObj> r1 = make<TObj>("foo");
    Ptr<TObj> p1 = make<TObj>("foo");
    Ptr<TObj> p2 = make<TObj>("foo");
    Ptr<TObj> p3(r1);
    Ptr<TObj> p4;
    Ptr<TObj> p5;
    EXPECT_NE(p1, r1);
    EXPECT_NE(r1, p1);
    EXPECT_EQ(r1, p3);
    EXPECT_EQ(p3, r1);
    EXPECT_NE(p1, p2);
    EXPECT_EQ(p4, p5);
}

} // namespace DLog
