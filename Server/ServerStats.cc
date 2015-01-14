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

ServerStats::ServerStats(Globals& globals)
    : globals(globals)
    , mutex()
    , enabled(false)
    , stats()
    , signalHandler()
    , timerHandler()
{
    signalHandler.reset(new SignalHandler(globals.eventLoop, *this));
}

ServerStats::~ServerStats()
{
}

void
ServerStats::enable()
{
    Lock lock(*this);
    if (!enabled) {
        // Defer construction of timer so that TimerHandler constructor can
        // access globals.config.
        timerHandler.reset(new TimerHandler(globals.eventLoop, *this));
        enabled = true;
    }
}

Protocol::ServerStats
ServerStats::getCurrent()
{
    Protocol::ServerStats copy;
    int64_t startTime = std::chrono::nanoseconds(
        Core::Time::SystemClock::now().time_since_epoch()).count();
    bool enabledCopy;
    {
        Lock lock(*this);
        copy = *lock;
        enabledCopy = enabled;
    }
    copy.set_start_at(startTime);
    if (enabledCopy) {
        globals.raft->updateServerStats(copy);
    }
    copy.set_end_at(std::chrono::nanoseconds(
        Core::Time::SystemClock::now().time_since_epoch()).count());
    return copy;
}

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

ServerStats::SignalHandler::SignalHandler(Event::Loop& eventLoop,
                                          ServerStats& serverStats)
    : Signal(eventLoop, SIGUSR1)
    , serverStats(serverStats)
{
}

void
ServerStats::SignalHandler::handleSignalEvent()
{
    NOTICE("Received %s: ServerStats:\n%s", strsignal(signalNumber),
           Core::ProtoBuf::dumpString(serverStats.getCurrent()).c_str());
}

ServerStats::TimerHandler::TimerHandler(Event::Loop& eventLoop,
                                        ServerStats& serverStats)
    : Timer(eventLoop)
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



} // namespace LogCabin::Server
} // namespace LogCabin
