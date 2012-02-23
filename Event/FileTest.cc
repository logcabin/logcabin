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

#include <event2/event.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include "include/Debug.h"
#include "Internal.h"
#include "File.h"

namespace LogCabin {
namespace Event {
namespace {

struct MyFile : public Event::File {
    explicit MyFile(Event::Loop& loop,
                    int fd,
                    uint32_t events)
        : File(loop, fd, events)
        , triggerCount(0)
    {
    }
    void handleFileEvent(uint32_t events) {
        ++triggerCount;
        eventLoop.exit();
    }
    uint32_t triggerCount;
};

struct EventFileTest : public ::testing::Test {
    EventFileTest()
        : loop()
        , pipeFds()
    {
        EXPECT_EQ(0, pipe(pipeFds));
    }
    ~EventFileTest()
    {
        closePipeFds();
    }

    void
    closePipeFds()
    {
        if (pipeFds[0] >= 0) {
            EXPECT_EQ(0, close(pipeFds[0]));
            pipeFds[0] = -1;
        }
        if (pipeFds[1] >= 0) {
            EXPECT_EQ(0, close(pipeFds[1]));
            pipeFds[1] = -1;
        }
    }
    Event::Loop loop;
    int pipeFds[2];
};

TEST_F(EventFileTest, constructor_badFileNotMonitored) {
    MyFile file(loop, -1, 0);
    EXPECT_TRUE(file.event != NULL);
    EXPECT_EQ(-1, file.fd);
    // not added
    EXPECT_EQ(0, event_pending(unqualify(file.event),
                               EV_READ,
                               NULL));
}

TEST_F(EventFileTest, destructor) {
    // Nothing to test.
}

TEST_F(EventFileTest, fires) {
    MyFile file(loop, pipeFds[0], File::Events::READABLE);
    EXPECT_EQ(1, write(pipeFds[1], "x", 1));
    loop.runForever();
    EXPECT_EQ(1U, file.triggerCount);
}

TEST_F(EventFileTest, setEvents) {
    MyFile file(loop, pipeFds[0], 0);
    EXPECT_EQ(1, write(pipeFds[1], "x", 1));
    EXPECT_EQ(0, event_pending(unqualify(file.event),
                               EV_READ,
                               NULL));
    file.setEvents(File::Events::READABLE);
    loop.runForever();
    EXPECT_EQ(1U, file.triggerCount);
    file.triggerCount = 0;
    file.setEvents(0);
    EXPECT_EQ(0, event_pending(unqualify(file.event),
                               EV_READ,
                               NULL));
}

TEST_F(EventFileTest, setEvents_badFileNotMonitored) {
    MyFile file(loop, pipeFds[0], File::Events::READABLE);
    EXPECT_EQ(1, write(pipeFds[1], "x", 1));
    file.setEvents(0);
    closePipeFds();
    EXPECT_EQ(0, event_pending(unqualify(file.event),
                               EV_READ,
                               NULL));
}

} // namespace LogCabin::Event::<anonymous>
} // namespace LogCabin::Event
} // namespace LogCabin
