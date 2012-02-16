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

#include <event2/listener.h>

#include "include/Debug.h"
#include "Event/Internal.h"
#include "RPC/TCPListener.h"

namespace LogCabin {
namespace RPC {

namespace {

/**
 * This is called by libevent when the listener accepts a new connection.
 */
void
onAccept(evconnlistener* libEventListener,
         evutil_socket_t socket,
         sockaddr* addr,
         int addrLen,
         void* listener)
{
    static_cast<TCPListener*>(listener)->handleNewConnection(socket);
}

} // anonymous namespace

TCPListener::TCPListener(Event::Loop& eventLoop,
                         const Address& listenAddress)
    : eventLoop(eventLoop)
    , listenAddress(listenAddress)
    , listener(NULL)
{
    if (!listenAddress.isValid()) {
        PANIC("Can't listen on address: %s",
              listenAddress.toString().c_str());
    }

    listener = qualify(evconnlistener_new_bind(
                    unqualify(eventLoop.base),
                    onAccept, this,
                    LEV_OPT_CLOSE_ON_FREE | LEV_OPT_CLOSE_ON_EXEC |
                    LEV_OPT_REUSEABLE | LEV_OPT_THREADSAFE,
                    -1 /* have libevent pick a sane default for backlog */,
                    listenAddress.getSockAddr(),
                    listenAddress.getSockAddrLen()));

    if (listener == NULL) {
        PANIC("evconnlistener_new_bind failed: "
              "Check to make sure the address is not in use.");
    }
}

TCPListener::~TCPListener()
{
    evconnlistener_free(unqualify(listener));
}

} // namespace LogCabin::RPC
} // namespace LogCabin
