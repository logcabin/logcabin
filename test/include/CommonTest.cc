/* Copyright (c) 2011-2012 Stanford University
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

#include <map>
#include <string>
#include <gtest/gtest.h>

#include "Common.h"

namespace DLog {

namespace {

using std::map;
using std::pair;
using std::string;
using std::vector;

class TObj {
  public:
    explicit TObj(const std::string& name) {
        EXPECT_EQ("foo", name);
        ++liveCount;
    }
    ~TObj() { --liveCount; }
    static uint32_t liveCount;
};
uint32_t TObj::liveCount;

const map<int, string> empty {};

const map<int, string> digits {
    { 1, "one" },
    { 2, "two" },
    { 3, "three" },
};

} // anonymous namespace

TEST(Common, unique) {
    TObj::liveCount = 0;
    {
        std::unique_ptr<TObj> x = unique<TObj>("foo");
        EXPECT_EQ(1U, TObj::liveCount);
    }
    EXPECT_EQ(0U, TObj::liveCount);
}

TEST(Common, downCast) {
    EXPECT_DEATH(downCast<uint8_t>(256), "");
    EXPECT_DEATH(downCast<int8_t>(192), "");
    EXPECT_DEATH(downCast<uint8_t>(-10), "");
    uint8_t x = downCast<uint8_t>(55UL);
    EXPECT_EQ(55, x);
}

TEST(Common, sorted) {
    EXPECT_EQ((vector<int> {}),
              sorted(vector<int> {}));
    EXPECT_EQ((vector<int> { 1, 5, 7}),
              sorted(vector<int> {5, 1, 7}));
}

TEST(Common, getKeys) {
    EXPECT_EQ((vector<int>{}),
              getKeys(empty));
    EXPECT_EQ((vector<int>{ 1, 2, 3 }),
              getKeys(digits));
}

TEST(Common, getValues) {
    EXPECT_EQ((vector<string>{}),
              getValues(empty));
    EXPECT_EQ((vector<string>{ "one", "two", "three" }),
              getValues(digits));
}

TEST(Common, getItems) {
    EXPECT_EQ((vector<pair<int, string>>{}),
              getItems(empty));
    EXPECT_EQ((vector<pair<int, string>>{
                    {1, "one"},
                    {2, "two"},
                    {3, "three"},
               }),
              getItems(digits));
}

TEST(Common, hasOnly) {
    EXPECT_TRUE(hasOnly((vector<int>{}), 1));
    EXPECT_TRUE(hasOnly((vector<int>{1}), 1));
    EXPECT_TRUE(hasOnly((vector<int>{1, 1}), 1));
    EXPECT_TRUE(hasOnly((vector<int>{1, 1, 1}), 1));
    EXPECT_FALSE(hasOnly((vector<int>{1, 2}), 1));
    EXPECT_FALSE(hasOnly((vector<int>{1, 2}), 2));
}

// Tests for format come from the RAMCloud project.
TEST(Common, formatBasic) {
    EXPECT_EQ("rofl3", format("rofl3"));
    EXPECT_EQ("rofl3", format("r%sl%d", "of", 3));
}

TEST(Common, formatLarge) {
    char x[3000];
    memset(x, 0xcc, sizeof(x));
    x[sizeof(x) - 1] = '\0';
    EXPECT_EQ(x, format("%s", x));
}

TEST(Common, isPrintableStr) {
    EXPECT_TRUE(isPrintable(""));
    EXPECT_TRUE(isPrintable("foo"));
    EXPECT_FALSE(isPrintable("\n"));
}

TEST(Common, isPrintableData) {
    EXPECT_FALSE(isPrintable("", 0));
    EXPECT_TRUE(isPrintable("", 1));
    EXPECT_TRUE(isPrintable("foo", 4));
    EXPECT_FALSE(isPrintable("foo", 3));
    EXPECT_FALSE(isPrintable("\n", 2));
}

} // namespace DLog
