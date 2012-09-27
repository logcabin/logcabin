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

#ifndef LOGCABIN_RPC_TCPLISTENER_H
#define LOGCABIN_RPC_TCPLISTENER_H

#include <vector>

#include "Event/Loop.h"
#include "Address.h"

// forward declaration
namespace LibEvent {
struct evconnlistener;
}

namespace LogCabin {
namespace RPC {

/**
 * A TCPListener listens for incoming TCP connections and accepts them.
 * The client should inherit from this and implement the handleNewConnection()
 * method for when a connection is accepted.
 *
 * TCPListeners can be created from any thread, but they will always run on
 * the thread running the Event::Loop.
 *
 * This is intended for use by RPC::Server only. It's not a very good
 * abstraction, but it encapsulates knowledge of libevent.
 */
class TCPListener {
  public:

    /**
     * Constructor. This object won't actually do anything until bind() is
     * called.
     * \param eventLoop
     *      Event::Loop that will manage this TCPListener object.
     */
    explicit TCPListener(Event::Loop& eventLoop);

    /**
     * Destructor.
     */
    virtual ~TCPListener();

    /**
     * Listen on a new address. You can call this multiple times to listen on
     * multiple addresses. (But if you call this twice with the same address,
     * the second time will always throw an error.)
     * This method is thread-safe.
     * \param listenAddress
     *      The address to listen on.
     * \return
     *      An error message if this was not able to listen on the given
     *      address; the empty string otherwise.
     */
    std::string bind(const Address& listenAddress);

    /**
     * This method is overridden by a subclass and invoked when a new
     * connection is accepted. This method will be invoked by the main event
     * loop on whatever thread is running the Event::Loop.
     *
     * The callee is in charge of closing the socket.
     */
    virtual void handleNewConnection(int socket) = 0;

    /**
     * Event::Loop that will manage this listener.
     */
    Event::Loop& eventLoop;

  private:

    /**
     * The listeners from libevent.
     * These are never NULL.
     */
    std::vector<LibEvent::evconnlistener*> listeners;

    // TCPListener is not copyable.
    TCPListener(const TCPListener&) = delete;
    TCPListener& operator=(const TCPListener&) = delete;
};

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_TCPLISTENER_H */
