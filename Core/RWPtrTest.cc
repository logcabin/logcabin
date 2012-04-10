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

#include <cinttypes>
#include <gtest/gtest.h>
#include <thread>

#include "Core/RWPtr.h"

namespace LogCabin {
namespace Core {
namespace {

void
lockAndIncrement(RWManager<uint64_t>& mgr, uint32_t times)
{
    for (uint32_t i = 0; i < times; ++i) {
        RWPtr<uint64_t> count = mgr.getExclusiveAccess();
        uint64_t start = *count;
        usleep(5);
        *count = start + 1;
    }
}

void
readShared(RWManager<uint64_t>& mgr, uint64_t* state, uint32_t times)
{
    for (*state = 1; *state <= times; ++(*state))
        mgr.getSharedAccess();
}

void
sample(RWManager<uint64_t>& mgr, uint64_t* value)
{
    *value = *mgr.getSharedAccess();
}

class CoreRWPtrTest : public ::testing::Test {
    CoreRWPtrTest()
        : mgr(new uint64_t(0))
    {
    }
    RWManager<uint64_t> mgr;
};

TEST_F(CoreRWPtrTest, compile) {
    // The point of this test is to instantiate all the templates to make sure
    // they type-check. This code isn't meant to be executed.
    if (0) {
        mgr.reset(new uint64_t(4));
        RWPtr<const uint64_t> read = mgr.getSharedAccess();
        RWPtr<const uint64_t> read2 = std::move(read);
        RWPtr<const uint64_t> read3;
        read3.get();
        read3 = std::move(read2);
        RWPtr<uint64_t> write = mgr.getExclusiveAccess();
        RWPtr<uint64_t> write2 = std::move(write);
        RWPtr<uint64_t> write3;
        write3 = std::move(write2);
        write3.get();
    }
}

TEST_F(CoreRWPtrTest, exclusiveAccess) {
    // Exclusive locks mutually exclude each other
    std::thread thread1(lockAndIncrement, std::ref(mgr), 100);
    std::thread thread2(lockAndIncrement, std::ref(mgr), 100);
    std::thread thread3(lockAndIncrement, std::ref(mgr), 100);
    thread1.join();
    thread2.join();
    thread3.join();
    EXPECT_EQ(300U, *mgr.getSharedAccess());
}

TEST_F(CoreRWPtrTest, sharedAccess) {
    // Shared locks allow each other to read simultaneously
    RWPtr<const uint64_t> r1 = mgr.getSharedAccess();
    RWPtr<const uint64_t> r2 = mgr.getSharedAccess();
    RWPtr<const uint64_t> r3 = mgr.getSharedAccess();
}

TEST_F(CoreRWPtrTest, sharedBlocksExclusive) {
    std::thread thread;
    {
        RWPtr<const uint64_t> r1 = mgr.getSharedAccess();
        thread = std::thread(lockAndIncrement, std::ref(mgr), 1);
        usleep(1000); // wait for the thread to start up
        EXPECT_EQ(0U, *r1);
    }
    thread.join();
    EXPECT_EQ(1U, *mgr.getSharedAccess());
}

TEST_F(CoreRWPtrTest, exclusiveBlocksShared) {
    std::thread thread;
    {
        RWPtr<uint64_t> r1 = mgr.getExclusiveAccess();
        uint64_t state = 0;
        thread = std::thread(readShared, std::ref(mgr), &state, 1);
        usleep(1000); // wait for the thread to start up
        EXPECT_EQ(1U, state);
        usleep(1000); // give it a chance to acquire the lock
        EXPECT_EQ(1U, state);
    }
    thread.join();
}

// A writer blocks new readers even before it has acquired the lock.
// Here's the situation we want to create:
//   Reader1 has the lock.
//   Writer wants the lock.
//   Reader2 wants the lock.
//   Reader1 releases the lock.
//   Writer gets & releases the lock. <-- Sets the value to 1.
//   Reader2 gets & releases the lock. <-- Reader2 should always read 1.
TEST_F(CoreRWPtrTest, exclusiveNotStarved) {
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t value = 1337;
        std::thread writer;
        std::thread reader;
        {
            RWPtr<const uint64_t> r1 = mgr.getSharedAccess();
            writer = std::thread(lockAndIncrement, std::ref(mgr), 1);
            usleep(1000); // wait for the thread to start up
            EXPECT_EQ(i, *r1);
            reader = std::thread(sample, std::ref(mgr), &value);
            usleep(1000); // wait for the thread to start up
            EXPECT_EQ(1337U, value);
        }
        writer.join();
        reader.join();
        EXPECT_EQ(i + 1, value);
    }
}

TEST_F(CoreRWPtrTest, reset) {
    mgr.reset(new uint64_t(4));
    EXPECT_EQ(4U, *mgr.getSharedAccess());
}


} // namespace LogCabin::Core::<anonymous>
} // namespace LogCabin::Core
} // namespace LogCabin
