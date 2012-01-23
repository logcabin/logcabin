/* Copyright (c) 2011 Stanford University
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

#include "DLogEvent.h"
#include "DLogRPC.h"

namespace DLog {
namespace RPC {

/**
 * Server connection object that processing incoming messages, creates
 * message objects, and calls the requested service.
 */
class ServerConnection : public EventSocket
{
  public:
    /**
     * Constructor.
     */
    ServerConnection();
    virtual ~ServerConnection();
    /**
     * Process incomin data that is pending from the socket.
     */
    virtual void read();
    /**
     * Notify us that socket is ready to transmit more data.
     */
    virtual void write();
    /**
     * Socket experienced a special event.
     */
    virtual void event(EventMask events, int errnum);
  private:
    Server *server;
    ServerConnection(const ServerConnection&) = delete;
    ServerConnection& operator=(const ServerConnection&) = delete;
};

/**
 * Server listener object that accepts new incoming requests.
 */
class ServerListener : public EventListener
{
  public:
    /**
     * Constructor.
     */
    ServerListener();
    virtual ~ServerListener();
    /**
     * A new connection was requested.
     */
    virtual void accept(int fd);
    /**
     * Process an error while trying to establish the listener socket.
     */
    virtual void error(void);
  private:
    Server *server;
    ServerListener(const ServerListener&) = delete;
    ServerListener& operator=(const ServerListener&) = delete;
};

} // namespace
} // namespace

