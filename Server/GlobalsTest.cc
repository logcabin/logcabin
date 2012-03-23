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

#include "Server/Globals.h"

namespace LogCabin {
namespace Server {
namespace {

TEST(ServerGlobalsTest, basics) {
    Globals globals;
    globals.config.set("storageModule", "memory");
    globals.config.set("uuid", "my-fake-uuid-123");
    globals.config.set("servers", "localhost");
    globals.init();
    globals.eventLoop.exit();
    globals.run();
}

TEST(ServerGlobalsTest, initNoServers) {
    Globals globals;
    globals.config.set("storageModule", "memory");
    globals.config.set("uuid", "my-fake-uuid-123");
    EXPECT_DEATH(globals.init(),
                 "No server addresses specified");
}

TEST(ServerGlobalsTest, initEmptyServers) {
    Globals globals;
    globals.config.set("storageModule", "memory");
    globals.config.set("uuid", "my-fake-uuid-123");
    globals.config.set("servers", ";");
    EXPECT_DEATH(globals.init(),
                 "invalid address");
}

TEST(ServerGlobalsTest, initAddressTaken) {
    Globals globals;
    globals.config.set("storageModule", "memory");
    globals.config.set("uuid", "my-fake-uuid-123");
    globals.config.set("servers", "localhost");
    globals.init();
    Globals globals2;
    globals2.config.set("storageModule", "memory");
    globals2.config.set("uuid", "my-fake-uuid-123");
    globals2.config.set("servers", "localhost");
    EXPECT_DEATH(globals2.init(),
                 "in use");
}

TEST(ServerGlobalsTest, initBindToOneOnly) {
    Globals globals;
    globals.config.set("storageModule", "memory");
    globals.config.set("uuid", "my-fake-uuid-123");
    globals.config.set("servers",
                       "google.com;localhost:61023;localhost:61024");
    globals.init();
    Globals globals2;
    globals2.config.set("storageModule", "memory");
    globals2.config.set("uuid", "my-fake-uuid-123");
    globals2.config.set("servers", "localhost:61024");
    globals2.init();
}

} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
