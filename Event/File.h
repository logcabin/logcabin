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

#ifndef LOGCABIN_EVENT_FILE_H
#define LOGCABIN_EVENT_FILE_H

#include "Loop.h"

namespace LogCabin {
namespace Event {

/**
 * A File is called by the Event::Loop when a file becomes readable or
 * writable. The client should inherit from this and implement the
 * handleFileEvent() method for when the file becomes readable or writable.
 *
 * File objects can be created from any thread, but they will always fire on
 * the thread running the Event::Loop.
 */
class File {
  public:

    /**
     * All possible file events. These are bitwise OR-ed together.
     */
    enum Events : uint32_t {
        /**
         * The file is readable.
         */
        READABLE = 0x1,
        /**
         * The file is writable.
         */
        WRITABLE = 0x2,
    };

    /**
     * Construct and enable monitoring a file descriptor.
     * \warning
     *      See the documentation for 'fd', which describes when the caller may
     *      safely close its file descriptor.
     * \param eventLoop
     *      Event::Loop that will manage this File object.
     * \param fd
     *      The file descriptor to monitor. The caller may only close this file
     *      descriptor if this object is never again used to monitor file
     *      events for the file (by setting events to 0 here or calling
     *      setEvents(0)).
     * \param events
     *      Invoke the object when any of the events specified by this
     *      parameter occur (OR-ed combination of File::Events values). If this
     *      is 0 then the file handler starts off inactive; it will not trigger
     *      until setEvents() has been called.
     */
    explicit File(Event::Loop& eventLoop, int fd, uint32_t events);

    /**
     * Destructor.
     */
    virtual ~File();

    /**
     * This method is overridden by a subclass and invoked when a file event
     * occurs. This method will be invoked by the main event loop on
     * whatever thread is running the Event::Loop.
     *
     * If the event still exists when this method returns (e.g., the file is
     * readable but the method did not read the data), then the method will be
     * invoked again.
     *
     * \param events
     *      Indicates whether the file is readable or writable or both (OR'ed
     *      combination of File::Events values).
     */
    virtual void handleFileEvent(uint32_t events) = 0;

    /**
     * Specify the events of interest for this file handler.
     *
     * This method behaves in a friendly way when called concurrently with file
     * events triggering:
     * - If this method is called from another thread and handleFileEvent() is
     *   running concurrently on the event loop thread, setEvents() will wait
     *   for handleFileEvent() to complete before returning.
     * - If this method is called from the event loop thread and
     *   handleFileEvent() is currently being fired, then setEvents() can't
     *   wait and returns immediately.
     *
     * \param events
     *      Indicates the conditions under which this object should be invoked
     *      (OR-ed combination of File::Events values). If this is 0 then the
     *      file handler is set to inactive; it will not trigger until
     *      setEvents() has been called.
     */
    void setEvents(uint32_t events);

    /**
     * Event::Loop that will manage this file.
     */
    Event::Loop& eventLoop;

    /**
     * The file descriptor to monitor.
     */
    const int fd;

  private:

    /**
     * The file event from libevent.
     * This is never NULL.
     */
    LibEvent::event* event;

    // File is not copyable.
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

} // namespace LogCabin::Event
} // namespace LogCabin

#endif /* LOGCABIN_EVENT_FILE_H */
