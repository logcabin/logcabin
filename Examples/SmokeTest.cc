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
#include <getopt.h>
#include <iostream>

#include "Client/Client.h"

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Entry;
using LogCabin::Client::Log;
using LogCabin::Client::Result;
using LogCabin::Client::Status;
using LogCabin::Client::Tree;

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , mock(false)
    {
        while (true) {
            static struct option longOptions[] = {
               {"mock",  no_argument, NULL, 'm'},
               {"help",  no_argument, NULL, 'h'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "hm", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'h':
                    usage();
                    exit(0);
                case 'm':
                    mock = true;
                    break;
                case '?':
                default:
                    // getopt_long already printed an error message.
                    usage();
                    exit(1);
            }
        }
    }

    void usage() {
        std::cout << "Usage: " << argv[0] << " [--mock]"
                  << std::endl;
        std::cout << "Options: " << std::endl;
        std::cout << "  -h, --help          "
                  << "Print this usage information" << std::endl;
        std::cout << "  -m, --mock          "
                  << "Instead of connecting to a LogCabin cluster, "
                  << "fake it with a local, in-memory implementation."
                  << std::endl;
    }

    int& argc;
    char**& argv;
    bool mock;
};

} // anonymous namespace

int
main(int argc, char** argv)
{
    OptionParser options(argc, argv);
    Cluster cluster = options.mock ? Cluster(Cluster::FOR_TESTING)
                                   : Cluster("logcabin:61023");

    Log log = cluster.openLog("smoketest");
    std::string hello = "hello world";
    Entry e(hello.c_str(), uint32_t(hello.length() + 1));
    assert(log.append(e) == 0);

    std::vector<Entry> entries = log.read(0);
    assert(entries.size() == 1U);
    assert(static_cast<const char*>(entries.at(0).getData()) == hello);
    assert(entries.at(0).getId() == 0);

    Tree tree = cluster.getTree();
    tree.makeDirectoryEx("/etc");
    tree.writeEx("/etc/passwd", "ha");
    std::string contents = tree.readEx("/etc/passwd");
    assert(contents == "ha");
    tree.removeDirectoryEx("/etc");

    return 0;
}
