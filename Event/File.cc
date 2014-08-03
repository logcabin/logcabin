/* Copyright (c) 2011-2014 Stanford University
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

#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Event/File.h"
#include "Event/Loop.h"

namespace LogCabin {
namespace Event {

File::File(Event::Loop& eventLoop, int fd, int fileEvents)
    : eventLoop(eventLoop)
    , fd(fd)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = fileEvents;
    event.data.ptr = this;
    int r = epoll_ctl(eventLoop.epollfd, EPOLL_CTL_ADD, fd, &event);
    if (r != 0) {
        PANIC("Adding file %d event with epoll_ctl failed: %s",
              fd, strerror(errno));
    }
}

File::~File()
{
    if (fd < 0)
        return;
    Event::Loop::Lock lock(eventLoop);
    int fd = release();
    int r = close(fd);
    if (r != 0)
        PANIC("Could not close file %d: %s", fd, strerror(errno));
}

void
File::setEvents(int fileEvents)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = fileEvents;
    event.data.ptr = this;
    int r = epoll_ctl(eventLoop.epollfd, EPOLL_CTL_MOD, fd, &event);
    if (r != 0) {
        PANIC("Modifying file %d event with epoll_ctl failed: %s",
              fd, strerror(errno));
    }
}

int
File::release()
{
    int r = epoll_ctl(eventLoop.epollfd, EPOLL_CTL_DEL, fd, NULL);
    if (r != 0) {
        PANIC("Removing file %d event with epoll_ctl failed: %s",
              fd, strerror(errno));
    }
    int released = fd;
    fd = -1;
    return released;
}

} // namespace LogCabin::Event
} // namespace LogCabin
