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

#include "Core/Debug.h"
#include "Protocol/Common.h"
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
    explicit Listener(Event::Loop& eventLoop)
        : TCPListener(eventLoop)
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
    Listener listener(loop);
    EXPECT_EQ("", listener.bind(Address("127.0.0.1", 61022)));
    Address address("127.0.0.1", Protocol::Common::DEFAULT_PORT);
    EXPECT_EQ("", listener.bind(address));
    EXPECT_EQ("", listener.bind(Address("127.0.0.1", 61024)));
    int connectError = -1;
    std::thread thread(connectThreadMain, address, &connectError);
    loop.runForever();
    EXPECT_EQ(1U, listener.count);
    thread.join();
    EXPECT_EQ(0, connectError);
}

TEST(RPCTCPListener, badAddress) {
    Event::Loop loop;
    Listener listener(loop);
    Address address("", 0);
    std::string error = listener.bind(address);
    EXPECT_TRUE(error.find("Can't listen on invalid address") != error.npos)
        << error;
}

TEST(RPCTCPListener, portTaken) {
    Event::Loop loop;
    Listener listener(loop);
    Address address("127.0.0.1", Protocol::Common::DEFAULT_PORT);
    EXPECT_EQ("", listener.bind(address));
    std::string error = listener.bind(address);
    EXPECT_TRUE(error.find("in use") != error.npos)
        << error;
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
