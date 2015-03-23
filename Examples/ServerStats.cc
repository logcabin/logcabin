/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2015 Diego Ongaro
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

#include <cassert>
#include <getopt.h>
#include <iostream>

#include <LogCabin/Client.h>
#include "Examples/Util.h"
#include "build/Protocol/ServerStats.pb.h"

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Result;
using LogCabin::Client::Status;
using LogCabin::Examples::Util::parseTime;

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , binary(false)
        , cluster("logcabin:61023")
        , timeout(parseTime("0s"))
        , servers()
    {
        while (true) {
            static struct option longOptions[] = {
               {"binary",  no_argument, NULL, 'b'},
               {"cluster",  required_argument, NULL, 'c'},
               {"timeout",  required_argument, NULL, 't'},
               {"help",  no_argument, NULL, 'h'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "bc:t:h", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'b':
                    binary = true;
                    break;
                case 'c':
                    cluster = optarg;
                    break;
                case 't':
                    timeout = parseTime(optarg);
                    break;
                case 'h':
                    usage();
                    exit(0);
                case '?':
                default:
                    // getopt_long already printed an error message.
                    usage();
                    exit(1);
            }
        }

        // Additional command line arguments are required.
        if (optind == argc) {
            usage();
            exit(1);
        }
        while (optind < argc) {
            servers.push_back(argv[optind]);
            ++optind;
        }
    }

    void usage() {
        std::cout
            << "Fetches information, metrics, and statistics from the given "
            << "servers. This can"
            << std::endl
            << "be useful in diagnosing problems."
            << std::endl
            << std::endl

            << "Usage: " << argv[0] << " [options] <server>..."
            << std::endl
            << std::endl

            << "Options:"
            << std::endl

            << "  -b, --binary                   "
            << "Outputs stats ProtoBuf in binary format"
            << std::endl
            << "                                 "
            << "(default is ProtoBuf text format)"
            << std::endl

            << "  -c <addresses>, --cluster=<addresses>  "
            << "Network addresses of the LogCabin"
            << std::endl
            << "                                         "
            << "servers, comma-separated"
            << std::endl
            << "                                         "
            << "[default: logcabin:61023]"
            << std::endl

            << "  -h, --help                     "
            << "Print this usage information"
            << std::endl

            << "  -t <time>, --timeout=<time>    "
            << "Set timeout for each server"
            << std::endl
            << "                                 "
            << "(0 means wait forever) [default: 0s]"
            << std::endl;
    }

    int& argc;
    char**& argv;
    bool binary;
    std::string cluster;
    uint64_t timeout;
    std::vector<std::string> servers;
};

} // anonymous namespace

int
main(int argc, char** argv)
{
    OptionParser options(argc, argv);
    Cluster cluster(options.cluster);
    for (auto it = options.servers.begin();
         it != options.servers.end();
         ++it) {
        std::string& server = *it;
        if (!options.binary) {
            std::cout << "Retrieving server stats from " << server << ":"
                      << std::endl;
        }
        LogCabin::Protocol::ServerStats stats;
        Result result = cluster.getServerStats(
                            server,
                            options.timeout,
                            stats);
        switch (result.status) {
            case Status::OK:
                if (options.binary) {
                    stats.SerializeToFileDescriptor(1 /* stdout */);
                } else {
                    std::cout << stats.DebugString();
                }
                break;
            case Status::TIMEOUT:
                std::cerr << "timed out" << std::endl;
                break;
            default:
                std::cerr << "Unknown status: " << result.error << std::endl;
                return 1;
        }
        if (!options.binary) {
            std::cout << std::endl;
        }
    }
    return 0;
}
