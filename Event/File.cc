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

#include "Core/Debug.h"
#include "Internal.h"
#include "Loop.h"
#include "File.h"

#include <event2/event.h>

namespace LogCabin {
namespace Event {

namespace {

/**
 * Convert from libevent's event mask type to File's.
 */
uint32_t
toFileEvents(LibEvent::EventMask libEventEvents)
{
    uint32_t fileEvents = 0;
    if (libEventEvents & EV_READ)
        fileEvents |= File::Events::READABLE;
    if (libEventEvents & EV_WRITE)
        fileEvents |= File::Events::WRITABLE;
    return fileEvents;
}

/**
 * Convert from File's event mask type to libevent's.
 */
LibEvent::EventMask
toLibEventEvents(uint32_t fileEvents)
{
    LibEvent::EventMask libEventEvents = EV_PERSIST;
    if (fileEvents & File::Events::READABLE)
        libEventEvents |= EV_READ;
    if (fileEvents & File::Events::WRITABLE)
        libEventEvents |= EV_WRITE;
    return libEventEvents;
}

/**
 * This is called by libevent when any file event fires.
 * \param fd
 *      Unused.
 * \param libEventEvents
 *      The bitwise OR of EV_READ, EV_WRITE, etc describing what has happened.
 * \param file
 *      The Event::File object whose file event fired.
 */
void
onFileEventFired(evutil_socket_t fd,
                 LibEvent::EventMask libEventEvents,
                 void* file)
{
    static_cast<File*>(file)->handleFileEvent(
                                    toFileEvents(libEventEvents));
}

} // anonymous namespace

File::File(Event::Loop& eventLoop, int fd, uint32_t fileEvents)
    : eventLoop(eventLoop)
    , fd(fd)
    , event(NULL)
{
    event = qualify(event_new(unqualify(eventLoop.base),
                              fd,
                              toLibEventEvents(fileEvents),
                              onFileEventFired, this));
    if (event == NULL) {
        PANIC("event_new failed: "
              "No information is available from libevent about this error.");
    }
    if (fileEvents == 0) {
        // If the user is not interested in any events, libevent should not
        // track the file descriptor. That way the user may safely close the
        // file.
        return;
    }
    int r = event_add(unqualify(event), NULL);
    if (r == -1) {
        PANIC("event_add failed: "
              "No information is available from libevent about this error.");
    }
}

File::~File()
{
    event_free(unqualify(event));
}

void
File::setEvents(uint32_t fileEvents)
{
    // This Lock is necessary for thread safety when multiple threads are
    // running within setEvents.
    Event::Loop::Lock lockGuard(eventLoop);

    int r = event_del(unqualify(event));
    if (r == -1) {
        PANIC("event_del failed: "
              "No information is available from libevent about this error.");
    }
    if (fileEvents == 0) {
        // If the user is not interested in any events, libevent should not
        // track the file descriptor. That way the user may safely close the
        // file.
        return;
    }
    r = event_assign(unqualify(event),
                     unqualify(eventLoop.base),
                     fd,
                     toLibEventEvents(fileEvents),
                     onFileEventFired, this);
    if (r == -1) {
        PANIC("event_assign failed: "
              "No information is available from libevent about this error.");
    }

    r = event_add(unqualify(event), NULL);
    if (r == -1) {
        PANIC("event_add failed: "
              "No information is available from libevent about this error.");
    }
}

} // namespace LogCabin::Event
} // namespace LogCabin
