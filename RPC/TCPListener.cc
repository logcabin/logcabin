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

#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "RPC/TCPListener.h"

namespace LogCabin {
namespace RPC {

////////// class TCPListener::BoundListener //////////

TCPListener::BoundListener::BoundListener(TCPListener& tcpListener,
                                          Event::Loop& eventLoop,
                                          int fd)
    : Event::File(eventLoop, fd, EPOLLIN)
    , tcpListener(tcpListener)
{
}

void
TCPListener::BoundListener::handleFileEvent(int events)
{
    int clientfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK|SOCK_CLOEXEC);
    if (clientfd < 0) {
        PANIC("Could not accept connection on fd %d: %s",
              fd, strerror(errno));
    }
    // TODO(ongaro): consider setting TCP_NODELAY
    tcpListener.handleNewConnection(clientfd);
}

////////// class TCPListener //////////

TCPListener::TCPListener(Event::Loop& eventLoop)
    : eventLoop(eventLoop)
    , mutex()
    , boundListeners()
{
}

TCPListener::~TCPListener()
{
    std::unique_lock<Core::Mutex> lock(mutex);
    boundListeners.clear();
}

std::string
TCPListener::bind(const Address& listenAddress)
{
    using Core::StringUtil::format;

    if (!listenAddress.isValid()) {
        return format("Can't listen on invalid address: %s",
                      listenAddress.toString().c_str());
    }

    int fd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if (fd < 0)
        PANIC("Could not create new TCP socket");

    int flag = 1;
    int r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                       &flag, sizeof(flag));
    if (r < 0) {
        PANIC("Could not set SO_REUSEADDR on socket: %s",
              strerror(errno));
    }


    r = ::bind(fd, listenAddress.getSockAddr(),
                   listenAddress.getSockAddrLen());
    if (r != 0) {
        std::string msg =
            format("Could not bind to address %s: %s%s",
                   listenAddress.toString().c_str(),
                   strerror(errno),
                   errno == EINVAL ? " (is the port in use?)" : "");
        r = close(fd);
        if (r != 0) {
            WARNING("Could not close socket that failed to bind: %s",
                    strerror(errno));
        }
        return msg;
    }

    // Why 128? No clue. It's what libevent was setting it to.
    r = listen(fd, 128);
    if (r != 0) {
        PANIC("Could not invoke listen() on address %s: %s",
              listenAddress.toString().c_str(),
              strerror(errno));
    }

    std::unique_lock<Core::Mutex> lock(mutex);
    boundListeners.emplace_back(*this, eventLoop, fd);
    return "";
}

} // namespace LogCabin::RPC
} // namespace LogCabin
