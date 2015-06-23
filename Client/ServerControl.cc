/* Copyright (c) 2015 Diego Ongaro
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
#include <string>
#include <vector>

#include "Client/ClientImpl.h"
#include "Core/ProtoBuf.h"
#include "build/Protocol/ServerControl.pb.h"
#include "include/LogCabin/Client.h"
#include "include/LogCabin/Util.h"

namespace LogCabin {
namespace Client {
namespace {

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , args()
        , lastIndex(0)
        , server("localhost:5254")
        , timeout(Client::Util::parseDuration("0s"))
    {
        while (true) {
            static struct option longOptions[] = {
               {"server",  required_argument, NULL, 's'},
               {"timeout",  required_argument, NULL, 't'},
               {"help",  no_argument, NULL, 'h'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "s:t:h", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 's':
                    server = optarg;
                    break;
                case 't':
                    timeout = Client::Util::parseDuration(optarg);
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

        args.assign(&argv[optind], &argv[argc]);
    }

    /**
     * Return the positional argument at the given index,
     * or panic if there were not enough arguments.
     */
    std::string at(uint64_t index) {
        if (args.size() <= index)
            usageError("Missing arguments");
        lastIndex = index;
        return args.at(index);
    }

    /**
     * Panic if are any unused arguments remain.
     */
    void done() {
        if (args.size() > lastIndex + 1)
            usageError("Too many arguments");
    }

    /**
     * Print an error and the usage message and exit nonzero.
     */
    void usageError(const std::string& message) {
        std::cerr << message << std::endl;
        usage();
        exit(1);
    }

    /**
     * Helper for spacing in usage() message.
     */
    std::string ospace(std::string option) {
        std::string after;
        if (option.size() < 31 - 2)
            after = std::string(31 - 2 - option.size(), ' ');
        return "  " + option + after;
    }

    void usage() {
        std::cout << "Inspect or modify the state of a single LogCabin server."
                  << std::endl
                  << "This program was added in LogCabin v1.1.0."
                  << std::endl;
        std::cout << std::endl;

        std::cout << "Usage: " << argv[0] << " [options] <command> [<args>]"
                  << std::endl;
        std::cout << std::endl;

        std::string space(31, ' ');
        std::cout << "Commands:" << std::endl;
        std::cout
            << ospace("info get")
            << "Print server ID and addresses."
            << std::endl

            << ospace("debug filename get")
            << "Print the server's debug log filename."
            << std::endl

            << ospace("debug filename set <path>")
            << "Change the server's debug log filename."
            << std::endl

            // TODO(ongaro): implement debug policy get/set
#if 0
            << ospace("debug policy get")
            << "Print the server's debug log policy."
            << std::endl

            << ospace("debug policy set <value>")
            << "Change the server's debug log policy."
            << std::endl
#endif

            << ospace("debug rotate")
            << "Rotate the server's debug log file."
            << std::endl

            // TODO(ongaro): implement snapshot inhibit
#if 0
            << ospace("snapshot inhibit get")
            << "Print the remaining time for which the server"
            << std::endl << space
            << "was asked to not snapshot."
            << std::endl

            << ospace("snapshot inhibit set <time>")
            << "Stop the snapshot from snapshotting for the"
            << std::endl << space
            << "given time duration."
            << std::endl
#endif

            // TODO(ongaro): implement snapshot commands
#if 0
            << ospace("snapshot start")
            << "Begin taking a snapshot if none is in progress."
            << std::endl

            << ospace("snapshot stop")
            << "Abort the current snapshot if one is in"
            << std::endl << space
            << "progress."
            << std::endl

            << ospace("snapshot restart")
            << "Abort the current snapshot if one is in"
            << std::endl << space
            << "progress, then begin taking a new snapshot."
            << std::endl
#endif

            << ospace("stats get")
            << "Print detailed server metrics."
            << std::endl

            << ospace("stats dump")
            << "Write detailed server metrics to server's debug"
            << std::endl << space
            << "log."
            << std::endl
            << std::endl;

        std::cout << "Options:" << std::endl;
        std::cout
            << ospace("-h, --help")
            << "Print this usage information and exit"
            << std::endl

            << "  -s <addresses>, --server=<addresses>  "
            << "Network addresses of the target"
            << std::endl
            << "                                        "
            << "LogCabin server, comma-separated"
            << std::endl
            << "                                        "
            << "[default: localhost:5254]"
            << std::endl

            << ospace("-t <time>, --timeout=<time>")
            << "Set timeout for the operation"
            << std::endl << space
            << "(0 means wait forever) [default: 0s]"
            << std::endl;
        // TODO(ongaro): human-readable vs machine-readable output?
    }

    int& argc;
    char**& argv;
    std::vector<std::string> args;
    uint64_t lastIndex;
    std::string server;
    uint64_t timeout;
};

/**
 * Print an error message and exit nonzero.
 */
void
error(const std::string& message)
{
    std::cerr << "Error: " << message << std::endl;
    exit(1);
}

namespace Proto = Protocol::ServerControl;

/**
 * Wrapper for invoking ServerControl RPCs.
 */
class ServerControl {
  public:
    ServerControl(const std::string& server, ClientImpl::TimePoint timeout)
        : clientImpl()
        , server(server)
        , timeout(timeout)
    {
        clientImpl.init("-INVALID-"); // shouldn't attempt to connect to this
    }

#define DEFINE_RPC(type, opcode) \
    void type(const Proto::type::Request& request, \
              Proto::type::Response& response) { \
        Result result = clientImpl.serverControl( \
                server, \
                timeout, \
                Proto::OpCode::opcode, \
                request, response); \
        if (result.status != Status::OK) { \
            error(result.error); \
        } \
    }

    DEFINE_RPC(DebugFilenameGet,       DEBUG_FILENAME_GET)
    DEFINE_RPC(DebugFilenameSet,       DEBUG_FILENAME_SET)
    DEFINE_RPC(DebugRotate,            DEBUG_ROTATE)
    DEFINE_RPC(ServerInfoGet,          SERVER_INFO_GET)
    DEFINE_RPC(ServerStatsDump,        SERVER_STATS_DUMP)
    DEFINE_RPC(ServerStatsGet,         SERVER_STATS_GET)

#undef DEFINE_RPC

    ClientImpl clientImpl;
    std::string server;
    ClientImpl::TimePoint timeout;
};

} // namespace LogCabin::Client::<anonymous>
} // namespace LogCabin::Client
} // namespace LogCabin


int
main(int argc, char** argv)
{
    using namespace LogCabin;
    using namespace LogCabin::Client;
    using Core::ProtoBuf::dumpString;
    Client::OptionParser options(argc, argv);
    ServerControl server(options.server,
                         ClientImpl::absTimeout(options.timeout));

    if (options.at(0) == "info") {
        if (options.at(1) == "get") {
            options.done();
            Proto::ServerInfoGet::Request request;
            Proto::ServerInfoGet::Response response;
            server.ServerInfoGet(request, response);
            std::cout << dumpString(response);
            return 0;
        }
    } else if (options.at(0) == "debug") {
        if (options.at(1) == "filename") {
            if (options.at(2) == "get") {
                options.done();
                Proto::DebugFilenameGet::Request request;
                Proto::DebugFilenameGet::Response response;
                server.DebugFilenameGet(request, response);
                std::cout << response.filename() << std::endl;
                return 0;
            } else if (options.at(2) == "set") {
                std::string value = options.at(3);
                options.done();
                Proto::DebugFilenameSet::Request request;
                Proto::DebugFilenameSet::Response response;
                request.set_filename(value);
                server.DebugFilenameSet(request, response);
                if (response.has_error())
                    error(response.error());
                return 0;
            }
        } else if (options.at(1) == "rotate") {
            options.done();
            Proto::DebugRotate::Request request;
            Proto::DebugRotate::Response response;
            server.DebugRotate(request, response);
            if (response.has_error())
                error(response.error());
            return 0;
        }
    } else if (options.at(0) == "stats") {
        if (options.at(1) == "get") {
            options.done();
            Proto::ServerStatsGet::Request request;
            Proto::ServerStatsGet::Response response;
            server.ServerStatsGet(request, response);
            std::cout << dumpString(response.server_stats());
            return 0;
        } else if (options.at(1) == "dump") {
            options.done();
            Proto::ServerStatsDump::Request request;
            Proto::ServerStatsDump::Response response;
            server.ServerStatsDump(request, response);
            return 0;
        }
    }
    options.usageError("Unknown command");
}
