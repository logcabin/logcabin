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

/**
 * \file
 * This file declares the interface for DLog's Event interface.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Ref.h"

#ifndef DLOGEVENT_H
#define DLOGEVENT_H

namespace DLog {
namespace RPC {

// Forward declarations
class EventLoop;
class EventSocketPriv;
class EventListenerPriv;
class EventSignalPriv;
class EventTimerPriv;

/**
 * A socket object that processes TCP socket events.
 */
class EventSocket {
  public:
    /*
     * EventMask flag, all values must be powers of two.
     */
    enum EventMask {
        Null = 0,
        Connected = 1,
        Eof = 2,
        Error = 4,
    };
    /**
     * Constructor.
     * \param loop EventLoop instance to bind to.
     */
    explicit EventSocket(EventLoop& loop);
    virtual ~EventSocket();
    /**
     * Bind to a pre-existing file descriptor. This is useful for wrapping
     * sockets that have been accepted.
     * \param fd Socket file descriptor.
     */
    bool bind(int fd);
    /**
     * Connect to a new machine.
     * \param ip IP address string to connect to.
     * \param port Port of the target machine.
     */
    bool connect(const char* ip, uint16_t port);
    /**
     * Socket read callback.
     */
    virtual void read() = 0;
    /**
     * Socket write callback.
     */
    virtual void write() = 0;
    /**
     * Socket event callback.
     */
    virtual void event(EventMask events, int errnum) = 0;
    /**
     * Write a specified number of bytes to the socket.
     */
    virtual int write(const void* buf, int length);
    /**
     * Set the read watermark.
     */
    virtual void setReadWatermark(int length);
  protected:
    /**
     * Get the length of the incoming data.
     */
    size_t getLength();
    /**
     * Read a specified number of bytes from the socket.
     */
    int read(void* buf, int length);
    /**
     * Discard a specified number of bytes from the socket.
     */
    int discard(int length);
  private:
    /// Pointer to the private implementation object.
    std::unique_ptr<EventSocketPriv> priv;
};

/**
 * A listener object that binds to a TCP socket and accepts new connections.
 */
class EventListener {
  public:
    /**
     * Constructor.
     * \param loop EventLoop instance to bind to.
     */
    explicit EventListener(EventLoop& loop);
    virtual ~EventListener();
    /**
     * Bind to a particular port and start listening.
     * \param port Port to bind to.
     */
    bool bind(uint16_t port);
    /**
     * Accept callback when a socket is accepted.
     * \param fd Incoming socket fd handle.
     */
    virtual void accept(int fd) = 0;
    /**
     * Error callback reporting something went wrong.
     */
    virtual void error(void) = 0;
  private:
    /// Pointer to the private implementation object.
    std::unique_ptr<EventListenerPriv> priv;
};

/**
 * A signal event object. The client should inherit from this and implement
 * the trigger method for when the signal is caught.
 */
class EventSignal {
  public:
    EventSignal(EventLoop& loop, int s);
    virtual ~EventSignal();
    void add();
    void add(time_t timeout);
    void remove();
    bool isPending();
    int getSignal();
    /**
     * The signal handler.
     */
    virtual void trigger() = 0;
  private:
    /// Signal to bind to.
    int signal;
    /// Pointer to the private implementation object.
    std::unique_ptr<EventSignalPriv> priv;
};

/**
 * A timer event object. The client should inherit from this and implement
 * the trigger method for when the timer expires.
 */
class EventTimer {
  public:
    explicit EventTimer(EventLoop& loop);
    virtual ~EventTimer();
    void addPeriodic(time_t seconds);
    void add(time_t seconds);
    void remove();
    bool isPending();
    bool isPersistent();
    time_t getPeriod();
    /**
     * The timer callback.
     */
    virtual void trigger() = 0;
  private:
    /// Used to store the period.
    time_t period;
    /// Set to true for periodic timers.
    bool persist;
    /// Pointer to the private implementation object.
    std::unique_ptr<EventTimerPriv> priv;
};

/**
 * Main event loop class.
 */
class EventLoop {
  public:
    /**
     * Constructor for the event loop implementation.
     *
     * TODO(ali): Support multiple event loop implementations.
     * TODO(ongaro): Who owns the memory for the returned object?
     */
    static EventLoop* makeEventLoop();
    virtual ~EventLoop() { };
    virtual void processEvents() = 0;
    virtual void loopBreak() = 0;
    virtual void loopExit(time_t seconds) = 0;
  protected:
    EventLoop() { };
  private:
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    friend class EventTimerLE2Priv;
};

} // namespace
} // namespace

#endif /* DLOGEVENT_H */
