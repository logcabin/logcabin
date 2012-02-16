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

#include <gtest/gtest.h>
#include <thread>

#include "include/Debug.h"
#include "RPC/TCPListener.h"

namespace LogCabin {
namespace RPC {
namespace {

/**
 * Attempts to connect to 'address'.
 */
void
connectThreadMain(Address address, int* connectError)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        *connectError = errno;
        return;
    }
    int r = connect(fd,
                    address.getSockAddr(),
                    address.getSockAddrLen());
    if (r == 0)
        *connectError = 0;
    else
        *connectError = errno;
    close(fd);
}

class Listener : public TCPListener {
    Listener(Event::Loop& eventLoop,
             const Address& listenAddress)
        : TCPListener(eventLoop, listenAddress)
        , count(0)
    {
    }
    void handleNewConnection(int socket) {
        close(socket);
        ++count;
        eventLoop.exit();
    }
    uint32_t count;
};

TEST(RPCTCPListener, basics) {
    Event::Loop loop;
    Address address("127.0.0.1", 61023);
    Listener listener(loop, address);
    int connectError = -1;
    std::thread thread(connectThreadMain, address, &connectError);
    loop.runForever();
    EXPECT_EQ(1U, listener.count);
    thread.join();
    EXPECT_EQ(0, connectError);
}

TEST(RPCTCPListener, badAddress) {
    Event::Loop loop;
    Address address("", 0);
    EXPECT_DEATH(Listener(loop, address),
                 "ERROR: Can't listen on address");
}

TEST(RPCTCPListener, portTaken) {
    Event::Loop loop;
    Address address("127.0.0.1", 61023);
    Listener ok(loop, address);
    EXPECT_DEATH(Listener(loop, address), "ERROR");
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
