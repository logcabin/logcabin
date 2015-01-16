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

#include <signal.h>

#include "Core/ProtoBuf.h"
#include "Core/Time.h"
#include "Event/Signal.h"
#include "Server/Globals.h"
#include "Server/RaftConsensus.h"
#include "Server/ServerStats.h"

namespace LogCabin {
namespace Server {

//// class ServerStats::Lock ////

ServerStats::Lock::Lock(ServerStats& wrapper)
    : wrapper(wrapper)
    , lockGuard()
{
}

ServerStats::Lock::~Lock()
{
}

Protocol::ServerStats*
ServerStats::Lock::operator->()
{
    return &wrapper.stats;
}

Protocol::ServerStats&
ServerStats::Lock::operator*()
{
    return wrapper.stats;
}

//// class ServerStats::SignalHandler ////

ServerStats::SignalHandler::SignalHandler(ServerStats& serverStats)
    : Signal(SIGUSR1)
    , serverStats(serverStats)
{
}

void
ServerStats::SignalHandler::handleSignalEvent()
{
    NOTICE("Received %s: ServerStats:\n%s", strsignal(signalNumber),
           Core::ProtoBuf::dumpString(serverStats.getCurrent()).c_str());
}

//// class ServerStats::TimerHandler ////

ServerStats::TimerHandler::TimerHandler(ServerStats& serverStats)
    : Timer()
    , serverStats(serverStats)
    , intervalNanos(1000 * 1000 *
                    serverStats.globals.config.read<uint64_t>(
                         "statsDumpIntervalMilliseconds", 60000))
{
    if (intervalNanos != 0)
        schedule(intervalNanos);
}

void
ServerStats::TimerHandler::handleTimerEvent()
{
    NOTICE("Periodic dump of ServerStats:\n%s",
           Core::ProtoBuf::dumpString(serverStats.getCurrent()).c_str());
    if (intervalNanos != 0)
        schedule(intervalNanos);
}

//// class ServerStats::Deferred ////

ServerStats::Deferred::Deferred(ServerStats& serverStats)
    : signalHandler(serverStats)
    , signalMonitor(serverStats.globals.eventLoop, signalHandler)
    , timerHandler(serverStats)
    , timerMonitor(serverStats.globals.eventLoop, timerHandler)
{
}

//// class ServerStats ////

ServerStats::ServerStats(Globals& globals)
    : globals(globals)
    , mutex()
    , stats()
    , deferred()
{
}

ServerStats::~ServerStats()
{
}

void
ServerStats::enable()
{
    Lock lock(*this);
    if (!deferred) {
        // Defer construction of timer so that TimerHandler constructor can
        // access globals.config.
        deferred.reset(new Deferred(*this));
    }
}

Protocol::ServerStats
ServerStats::getCurrent()
{
    Protocol::ServerStats copy;
    int64_t startTime = std::chrono::nanoseconds(
        Core::Time::SystemClock::now().time_since_epoch()).count();
    bool enabled;
    {
        Lock lock(*this);
        copy = *lock;
        enabled = (deferred.get() != NULL);
    }
    copy.set_start_at(startTime);
    if (enabled) {
        globals.raft->updateServerStats(copy);
    }
    copy.set_end_at(std::chrono::nanoseconds(
        Core::Time::SystemClock::now().time_since_epoch()).count());
    return copy;
}



} // namespace LogCabin::Server
} // namespace LogCabin
