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
#include <sstream>

#include <LogCabin/Client.h>
#include <LogCabin/Util.h>

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Tree;

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , cluster("logcabin:5254")
        , timeout(LogCabin::Client::Util::parseDuration("10s"))
    {
        while (true) {
            static struct option longOptions[] = {
               {"cluster",  required_argument, NULL, 'c'},
               {"timeout",  required_argument, NULL, 't'},
               {"help",  no_argument, NULL, 'h'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "c:t:h", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'c':
                    cluster = optarg;
                    break;
                case 't':
                    timeout = LogCabin::Client::Util::parseDuration(optarg);
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
    }

    void usage() {
        std::cout
            << "Executes a bunch of reads and writes against a LogCabin "
            << "cluster, which are"
            << std::endl
            << "periodically verified. This is intended to be executed using"
            << std::endl
            << "scripts/failovertest.py, which kills LogCabin servers in the "
            << "meantime."
            << std::endl
            << std::endl

            << "Usage: " << argv[0] << " [options]"
            << std::endl
            << std::endl

            << "Options:"
            << std::endl

            << "  -c <addresses>, --cluster=<addresses>  "
            << "Network addresses of the LogCabin"
            << std::endl
            << "                                         "
            << "servers, comma-separated"
            << std::endl
            << "                                         "
            << "[default: logcabin:5254]"
            << std::endl

            << "  -h, --help                     "
            << "Print this usage information"
            << std::endl

            << "  -t <time>, --timeout=<time>    "
            << "Set timeout for individual read and write"
            << std::endl
            << "                                 "
            << "operations [default: 10s]"
            << std::endl;
    }

    int& argc;
    char**& argv;
    std::string cluster;
    uint64_t timeout;
};

std::string
toString(uint64_t v)
{
    char buf[17];
    snprintf(buf,
             sizeof(buf),
             "%016lx",
             v);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

uint64_t
toU64(const std::string& s)
{
    return strtoull(s.c_str(), NULL, 16);
}

void
verify(Tree& tree)
{
    std::vector<std::string> keys = tree.listDirectoryEx(".");
    assert(keys.size() >= 2);
    auto it = keys.begin();
    assert(*it == "0000000000000000");
    assert(tree.readEx(*it) == "0000000000000001");
    ++it;
    assert(*it == "0000000000000001");
    assert(tree.readEx(*it) == "0000000000000001");
    ++it;
    while (it != keys.end()) {
        std::string key = *it;
        uint64_t i = toU64(key);
        uint64_t a = toU64(tree.readEx(toString(i - 2)));
        uint64_t b = toU64(tree.readEx(toString(i - 1)));
        assert(toU64(tree.readEx(key)) == a + b);
        ++it;
    }
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    OptionParser options(argc, argv);
    Cluster cluster(options.cluster);
    Tree tree = cluster.getTree();
    tree.setTimeout(options.timeout);
    tree.setWorkingDirectoryEx("/failovertest");
    tree.writeEx("0000000000000000", "0000000000000001");
    tree.writeEx("0000000000000001", "0000000000000001");
    uint64_t i = 2;
    while (true) {
        if ((i & (i-1)) == 0) { // powers of two
            std::cout << "i=" << i << std::endl;
            verify(tree);
        }
        std::string key = toString(i);
        uint64_t a = toU64(tree.readEx(toString(i - 2)));
        uint64_t b = toU64(tree.readEx(toString(i - 1)));
        tree.writeEx(key, toString(a + b));
        ++i;
    }
}
