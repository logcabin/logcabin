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

#include <memory>

#include "Core/Config.h"
#include "Core/RWPtr.h"
#include "Event/Loop.h"
#include "Event/Signal.h"

#ifndef LOGCABIN_SERVER_GLOBALS_H
#define LOGCABIN_SERVER_GLOBALS_H

namespace LogCabin {

// forward declarations
namespace RPC {
class Server;
}

namespace Server {

// forward declarations
class RaftConsensus;
class RaftService;
class ClientService;
class LogManager;
class StateMachine;

/**
 * Holds the LogCabin daemon's top-level objects.
 * The purpose of main() is to create and run a Globals object.
 * Other classes may refer to this object if they need access to other
 * top-level objects.
 */
class Globals {
  private:
    /**
     * Exits from the event loop upon receiving a UNIX signal.
     */
    class ExitHandler : public Event::Signal {
      public:
        ExitHandler(Event::Loop& eventLoop, int signalNumber);
        void handleSignalEvent();
    };

  public:

    /// Constructor.
    Globals();

    /// Destructor.
    ~Globals();

    /**
     * Finish initializing this object.
     * This should be called after #config has been filled in.
     */
    void init(uint64_t serverId = 0);

    /**
     * Run the event loop until SIGINT, SIGTERM, or someone calls
     * Event::Loop::exit().
     */
    void run();

    /**
     * Global configuration options.
     */
    Core::Config config;

    /**
     * The event loop that runs the RPC system.
     */
    Event::Loop eventLoop;

  private:
    /**
     * Exits the event loop upon receiving SIGINT (keyboard interrupt).
     */
    ExitHandler sigIntHandler;

    /**
     * Exits the event loop upon receiving SIGTERM (kill).
     */
    ExitHandler sigTermHandler;

  public:
    /**
     * Used by the client service for managing and accessing logs.
     */
    Core::RWManager<LogManager> logManager;

    /**
     * Consensus module.
     */
    std::shared_ptr<Server::RaftConsensus> raft;

    /**
     * State machine used to process client requests.
     */
    std::shared_ptr<Server::StateMachine> stateMachine;

  private:

    /**
     * Service used to communicate between servers.
     */
    std::shared_ptr<Server::RaftService> raftService;

    /**
     * The application-facing facing RPC service.
     */
    std::shared_ptr<Server::ClientService> clientService;

    /**
     * Listens for inbound RPCs and passes them off to the services.
     */
    std::unique_ptr<RPC::Server> rpcServer;

    // Globals is non-copyable.
    Globals(const Globals&) = delete;
    Globals& operator=(const Globals&) = delete;

}; // class Globals

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_GLOBALS_H */
