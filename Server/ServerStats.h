/* Copyright (c) 2015 Diego Ongaro
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

#ifndef LOGCABIN_SERVER_SERVERSTATS_H
#define LOGCABIN_SERVER_SERVERSTATS_H

#include <memory>

#include "build/Protocol/ServerStats.pb.h"

#include "Core/Mutex.h"
#include "Event/Signal.h"
#include "Event/Timer.h"

namespace LogCabin {
namespace Server {

// forward declaration
class Globals;

/**
 * Manages creation of server statistics, which are used for diagnostics.
 *
 * Server statistics are gathered in two ways. First, this object maintains a
 * #stats structure that modules can fill in by acquiring a Lock and modifying
 * directly. This #stats structure is copied every time stats are requested.
 * Second, when stats are requested, getCurrent() will ask certain modules
 * (such as RaftConsensus) to fill in the current information into a stats
 * structure.
 */
class ServerStats {
  public:
    /// Constructor.
    explicit ServerStats(Globals& globals);
    /// Destructor.
    ~ServerStats();

    /**
     * Called after Globals are initialized to finish setting up this class.
     * Attaches signal handler and sets up timer. Starts calling other modules
     * for their state in getCurrent().
     */
    void enable();

    /**
     * Calculate and return the current server stats.
     */
    Protocol::ServerStats getCurrent();

    /**
     * Provides read/write access to #stats, protected against concurrent
     * access.
     */
    class Lock {
      public:
        /// Constructor.
        explicit Lock(ServerStats& wrapper);
        /// Destructor.
        ~Lock();
        /// Structure dereference operator. Returns stats pointer.
        Protocol::ServerStats* operator->();
        /// Indirection (dereference) operator. Returns stats reference.
        Protocol::ServerStats& operator*();
      private:
        /// Handle to containing class.
        ServerStats& wrapper;
        /// Locks #mutex for the lifetime of this object.
        std::unique_lock<Core::Mutex> lockGuard;
    };

  private:
    /**
     * Dumps stats to the debug log (NOTICE level) on SIGUSR1 signal.
     */
    class SignalHandler : public Event::Signal {
      public:
        /// Constructor. Registers itself as SIGUSR1 handler.
        explicit SignalHandler(ServerStats& serverStats);
        /// Fires when SIGUSR1 is received. Prints the stats to the log.
        void handleSignalEvent();
        /// Handle to containing class.
        ServerStats& serverStats;
    };

    /**
     * Dumps stats to the debug log (NOTICE level) periodically.
     */
    class TimerHandler : public Event::Timer {
      public:
        /// Constructor. Begins periodic timer.
        explicit TimerHandler(ServerStats& serverStats);
        /// Fires when timer expires. Prints the stats to the log.
        void handleTimerEvent();
        /// Handle to containing class.
        ServerStats& serverStats;
        /// How often to dump the stats to the log, in nanoseconds. 0 indicates
        /// never.
        uint64_t intervalNanos;
    };

    /**
     * Members that are constructed later, during enable().
     * Whereas the ServerStats is constructed early in the server startup
     * process, these members get to access globals and globals.config in their
     * constructors.
     */
    struct Deferred {
        /// Constructor. Called during enable().
        explicit Deferred(ServerStats& serverStats);
        /// See SignalHandler.
        SignalHandler signalHandler;
        /// Registers signalHandler with event loop.
        Event::Signal::Monitor signalMonitor;
        /// See TimerHandler.
        TimerHandler timerHandler;
        /// Registers timerHandler with event loop.
        Event::Timer::Monitor timerMonitor;
    };

    /**
     * Server-wide objects.
     */
    Globals& globals;
    /**
     * Protects all of the following members of this class.
     */
    Core::Mutex mutex;

    /**
     * Partially filled-in structure that is copied as the basis of all calls
     * to getCurrent().
     */
    Protocol::ServerStats stats;

    /**
     * See Deferred. If non-NULL, enabled() has already been called, and other
     * modules should be queried for stats during getCurrent().
     */
    std::unique_ptr<Deferred> deferred;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_SERVERSTATS_H */
