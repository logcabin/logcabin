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

#include <iostream>

#include "Client/Client.h"

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Entry;
using LogCabin::Client::Log;

void
printLogContents(Log& log, const char* logName)
{
    std::vector<Entry> entries = log.read(0);
    std::cout << "Log " << logName << ":" << std::endl;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        std::cout << "- " << it->getId() << ": "
                  << std::string(static_cast<const char*>(it->getData()),
                                 it->getLength())
                  << std::endl;
    }
    std::cout << std::endl;
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    Cluster cluster("localhost:61023");
    std::vector<std::string> logNames = cluster.listLogs();
    std::cout << "Logs:" << std::endl;
    for (auto it = logNames.begin(); it != logNames.end(); ++it)
        std::cout << "- " << *it << std::endl;
    std::cout << std::endl;

    Log log = cluster.openLog("greetings");
    std::string hello = "hello world";
    Entry entry(hello.c_str(), uint32_t(hello.length() + 1));

    printLogContents(log, "greetings");
    for (int i = 0; i < 10; ++i) {
        log.append(entry);
        printLogContents(log, "greetings");
    }
}
