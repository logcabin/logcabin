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

#ifndef LOGCABIN_EVENT_FILE_H
#define LOGCABIN_EVENT_FILE_H

#include "Event/Loop.h"

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
     * Construct and enable monitoring a file descriptor.
     * \param eventLoop
     *      Event::Loop that will manage this File object.
     * \param fd
     *      A file descriptor of a valid open file to monitor. Unless release()
     *      is called first, this File object will close the file descriptor
     *      when it is destroyed.
     * \param events
     *      Invoke the handler when any of the events specified by this
     *      parameter occur (OR-ed combination of EPOLL_EVENTS values). If this
     *      is 0 then the file handler starts off inactive; it will not trigger
     *      until setEvents() has been called (except possibly for errors such
     *      as EPOLLHUP; these events are always active).
     */
    explicit File(Event::Loop& eventLoop, int fd, int events);

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
     * invoked again (unless flags such as EPOLLONESHOT or EPOLLET are used).
     *
     * \param events
     *      Indicates whether the file is readable or writable or both (OR'ed
     *      combination of EPOLL_EVENTS values).
     */
    virtual void handleFileEvent(int events) = 0;

    /**
     * Remove the file descriptor from being monitored by this object. Unless
     * running in the event loop thread, a caller must typically take an
     * Event::Loop::Lock before closing the file descriptor, to ensure that a
     * concurrent handler call does not access the closed file.
     * This may only be called once.
     */
    int release();

    /**
     * Specify the events of interest for this file handler.
     * This method is safe to call concurrently with file events triggering. If
     * called outside an event handler and no Event::Loop::Lock is taken, this
     * may have a short delay while active handlers continue to be called.
     *
     * \param events
     *      Indicates the conditions under which this object should be invoked
     *      (OR-ed combination of EPOLL_EVENTS values). If this is 0 then the
     *      file handler is set to inactive; it will not trigger until
     *      setEvents() has been called (except possibly for errors such as
     *      EPOLLHUP; these events are always active).
     */
    void setEvents(int events);

    /**
     * Event::Loop that will manage this file.
     */
    Event::Loop& eventLoop;

    /**
     * The file descriptor to monitor.
     */
    int fd;

  private:
    // File is not copyable.
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

} // namespace LogCabin::Event
} // namespace LogCabin

#endif /* LOGCABIN_EVENT_FILE_H */
