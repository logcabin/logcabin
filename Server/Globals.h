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

#include "include/Config.h"
#include "Event/Loop.h"
#include "Event/Signal.h"

#ifndef LOGCABIN_SERVER_GLOBALS_H
#define LOGCABIN_SERVER_GLOBALS_H

namespace LogCabin {

// forward declarations
namespace RPC {
class ThreadDispatchService;
class Server;
}

namespace Server {

// forward declaration
class ClientService;

/**
 * Holds the LogCabin daemon's top-level objects.
 * The purpose of main() is to create and run a Globals object.
 * Other classes may refer to this object if they need access to other
 * top-level objects.
 */
class Globals {
  private:
    /**
     * Turns SIGINT signals (such as keyboard interrupt) into an exit from
     * an event loop.
     */
    class SigIntHandler : public Event::Signal {
      public:
        explicit SigIntHandler(Event::Loop& eventLoop);
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
    void init();

    /**
     * Run the event loop until SIGINT or someone calls Event::Loop::exit().
     */
    void run();

    /**
     * Global configuration options.
     */
    DLog::Config config;

    /**
     * The event loop that runs the RPC system.
     */
    Event::Loop eventLoop;

  private:
    /**
     * Turns SIGINT signals (such as keyboard interrupt) into an exit from
     * #eventLoop.
     */
    SigIntHandler sigIntHandler;

    /**
     * The application-facing facing RPC service.
     */
    std::unique_ptr<Server::ClientService> clientService;

    /**
     * Dispatches RPCs to #clientService on multiple worker threads.
     */
    std::unique_ptr<RPC::ThreadDispatchService> dispatchService;

    /**
     * Listens for inbound RPCs from applications and passes them off to
     * #dispatchService.
     */
    std::unique_ptr<RPC::Server> rpcServer;

    // Globals is non-copyable.
    Globals(const Globals&) = delete;
    Globals& operator=(const Globals&) = delete;

}; // class Globals

} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_GLOBALS_H */
