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

/**
 * \file
 * This is a basic end-to-end test of LogCabin.
 */

#include <cassert>

#include "Client/Client.h"

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Entry;
using LogCabin::Client::Log;

} // anonymous namespace

int
main(int argc, char** argv)
{
    Cluster cluster("logcabin:61023");

    Log log = cluster.openLog("smoketest");
    std::string hello = "hello world";
    Entry e(hello.c_str(), uint32_t(hello.length() + 1));
    assert(log.append(e) == 0);

    std::vector<Entry> entries = log.read(0);
    assert(entries.size() == 1U);
    assert(static_cast<const char*>(entries.at(0).getData()) == hello);
    assert(entries.at(0).getId() == 0);
    return 0;
}
