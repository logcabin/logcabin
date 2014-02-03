/* Copyright (c) 2012-2014 Stanford University
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
 * This is a basic latency/bandwidth benchmark of LogCabin.
 */

#include <cassert>
#include <ctime>
#include <getopt.h>
#include <iostream>
#include <thread>

#include "Client/Client.h"

namespace {

using LogCabin::Client::Cluster;
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
        , cluster("logcabin:61023")
        , size(1024)
        , writers(1)
        , totalWrites(1000)
    {
        while (true) {
            static struct option longOptions[] = {
               {"cluster",  required_argument, NULL, 'c'},
               {"help",  no_argument, NULL, 'h'},
               {"size",  required_argument, NULL, 's'},
               {"threads",  required_argument, NULL, 't'},
               {"writes",  required_argument, NULL, 'w'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "c:hs:t:w:", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'c':
                    cluster = optarg;
                    break;
                case 'h':
                    usage();
                    exit(0);
                case 's':
                    size = uint64_t(atol(optarg));
                    break;
                case 't':
                    writers = uint64_t(atol(optarg));
                    break;
                case 'w':
                    totalWrites = uint64_t(atol(optarg));
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
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
        std::cout << "Options: " << std::endl;
        std::cout << "  -c, --cluster <address> "
                  << "The network address of the LogCabin cluster "
                  << "(default: logcabin:61023)" << std::endl;
        std::cout << "  -h, --help              "
                  << "Print this usage information" << std::endl;
        std::cout << "  --size <bytes>          "
                  << "Size of value in each write [default: 1024]"
                  << std::endl;
        std::cout << "  --threads <num>         "
                  << "Number of concurrent writers [default: 1]"
                  << std::endl;
        std::cout << "  --writes <num>          "
                  << "Number of writes total writes [default: 1000]"
                  << std::endl;
    }

    int& argc;
    char**& argv;
    std::string cluster;
    uint64_t size;
    uint64_t writers;
    uint64_t totalWrites;
};

void
writeThreadMain(uint64_t id,
                const OptionParser& options,
                Tree tree,
                const std::string& key,
                const std::string& value)
{
    uint64_t numWrites = options.totalWrites / options.writers;
    if (id == 0) { // thread 0 takes any odd leftover writes
        numWrites = options.totalWrites - numWrites * (options.writers - 1);
    }
    for (uint64_t i = 0; i < numWrites; ++i) {
        tree.writeEx(key, value);
    }
}

uint64_t timeNanos()
{
    struct timespec now;
    int r = clock_gettime(CLOCK_REALTIME, &now);
    assert(r == 0);
    return uint64_t(now.tv_sec) * 1000 * 1000 * 1000 + now.tv_nsec;
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    OptionParser options(argc, argv);
    Cluster cluster = Cluster(options.cluster);
    Tree tree = cluster.getTree();

    std::string key("/bench");
    std::string value(options.size, 'v');

    uint64_t startNanos = timeNanos();
    std::vector<std::thread> threads;
    for (uint64_t i = 0; i < options.writers; ++i) {
        threads.emplace_back(writeThreadMain, i, std::ref(options),
                             tree, std::ref(key), std::ref(value));
    }
    for (uint64_t i = 0; i < options.writers; ++i) {
        threads.at(i).join();
    }
    uint64_t endNanos = timeNanos();

    tree.removeFile(key);
    std::cout << "Benchmark took "
              << static_cast<double>(endNanos - startNanos) / 1e6 << " ms"
              << std::endl;
    return 0;
}
