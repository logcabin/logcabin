/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014 Diego Ongaro
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

#include <getopt.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "Core/Debug.h"
#include "Core/ThreadId.h"
#include "Server/Globals.h"
#include "Server/RaftConsensus.h"

namespace {

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , bootstrap(false)
        , configFilename("logcabin.conf")
        , daemon(false)
        , debugLogFilename() // empty for default
        , pidFilename() // empty for none
        , serverId(1)
    {
        while (true) {
            static struct option longOptions[] = {
               {"bootstrap",  no_argument, NULL, 'b'},
               {"config",  required_argument, NULL, 'c'},
               {"daemon",  no_argument, NULL, 'd'},
               {"help",  no_argument, NULL, 'h'},
               {"id",  required_argument, NULL, 'i'},
               {"log",  required_argument, NULL, 'l'},
               {"pidfile",  required_argument, NULL, 'p'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "bc:dhi:l:p:", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'h':
                    usage();
                    exit(0);
                case 'b':
                    bootstrap = true;
                    break;
                case 'c':
                    configFilename = optarg;
                    break;
                case 'd':
                    daemon = true;
                    break;
                case 'i':
                    serverId = uint64_t(atol(optarg));
                    break;
                case 'l':
                    debugLogFilename = optarg;
                    break;
                case 'p':
                    pidFilename = optarg;
                    break;
                case '?':
                default:
                    // getopt_long already printed an error message.
                    usage();
                    exit(1);
            }
        }

        // We don't expect any additional command line arguments (not options).
        if (optind != argc) {
            usage();
            exit(1);
        }
    }

    void usage() {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
        std::cout << "Options: " << std::endl;
        std::cout << "  -h, --help            "
                  << "Print this usage information" << std::endl;
        std::cout << "  --bootstrap           "
                  << "Write a cluster configuration into the very first "
                  << "server's log and exit. This should only be run once "
                  << "ever in each cluster" << std::endl;
        std::cout << "  -c, --config <file>   "
                  << "Specify the configuration file "
                  << "(default: logcabin.conf)" << std::endl;
        std::cout << "  -d, --daemon          "
                  << "Detach and run in the background (requires --log)"
                  << std::endl;
        std::cout << "  -i, --id <id>         "
                  << "Set server id to <id> "
                  << "(default: index of first bindable address + 1)"
                  << std::endl;
        std::cout << "  -l, --log <file>      "
                  << "Write debug logs to <file> "
                  << "(default: stderr)"
                  << std::endl;
        std::cout << "  -p, --pidfile <file>  "
                  << "Write process ID to <file>"
                  << std::endl;
    }

    int& argc;
    char**& argv;
    bool bootstrap;
    std::string configFilename;
    bool daemon;
    std::string debugLogFilename;
    std::string pidFilename;
    uint64_t serverId;
};

/**
 * RAII-style class to manage a file containing the process ID.
 */
class PidFile {
  public:
    explicit PidFile(const std::string& filename)
        : filename(filename)
        , written(-1)
    {
    }

    ~PidFile() {
        removeFile();
    }

    void writePid(int pid) {
        if (filename.empty())
            return;
        FILE* file = fopen(filename.c_str(), "w");
        if (file == NULL) {
            PANIC("Could not open %s for writing process ID: %s",
                  filename.c_str(),
                  strerror(errno));
        }
        std::string pidString =
            LogCabin::Core::StringUtil::format("%d\n", pid);
        size_t bytesWritten =
            fwrite(pidString.c_str(), 1, pidString.size(), file);
        if (bytesWritten != pidString.size()) {
            PANIC("Could not write process ID %s to pidfile %s: %s",
                  pidString.c_str(), filename.c_str(),
                  strerror(errno));
        }
        int r = fclose(file);
        if (r != 0) {
            PANIC("Could not close pidfile %s: %s",
                  filename.c_str(),
                  strerror(errno));
        }
        NOTICE("Wrote PID %d to %s",
               pid, filename.c_str());
        written = pid;
    }

    void removeFile() {
        if (written < 0)
            return;
        FILE* file = fopen(filename.c_str(), "r");
        if (file == NULL) {
            WARNING("Could not open %s for reading process ID prior to "
                    "removal: %s",
                    filename.c_str(),
                    strerror(errno));
            return;
        }
        char readbuf[10];
        memset(readbuf, 0, sizeof(readbuf));
        size_t bytesRead = fread(readbuf, 1, sizeof(readbuf), file);
        if (bytesRead == 0) {
            WARNING("PID could not be read from pidfile: "
                    "will not remove file %s",
                    filename.c_str());
            return;
        }
        int pidRead = atoi(readbuf);
        if (pidRead != written) {
            WARNING("PID read from pidfile (%d) does not match PID written "
                    "earlier (%d): will not remove file %s",
                    pidRead, written, filename.c_str());
            return;
        }
        int r = unlink(filename.c_str());
        if (r != 0) {
            WARNING("Could not unlink %s: %s",
                    filename.c_str(), strerror(errno));
            return;
        }
        written = -1;
        NOTICE("Removed pidfile %s", filename.c_str());
    }

    std::string filename;
    int written;
};

} // anonymous namespace

int
main(int argc, char** argv)
{
    using namespace LogCabin;

    Core::ThreadId::setName("evloop");
    //Core::Debug::setLogPolicy({{"Server", "VERBOSE"}});

    // Parse command line args.
    OptionParser options(argc, argv);

    // Set debug log file
    if (!options.debugLogFilename.empty()) {
        FILE* debugLog = fopen(options.debugLogFilename.c_str(), "a");
        if (debugLog == NULL) {
            PANIC("Could not open %s for writing debug log messages: %s",
                  options.debugLogFilename.c_str(),
                  strerror(errno));
        }
        Core::Debug::setLogFile(debugLog);
    }

    NOTICE("Using config file %s", options.configFilename.c_str());

    // Detach as daemon
    if (options.daemon) {
        if (options.debugLogFilename.empty()) {
            PANIC("Refusing to run as daemon without a log file "
                  "(use /dev/null if you insist)");
        }
        NOTICE("Detaching");
        bool chdir = false; // leave the current working directory in case the
                            // user has specified relative paths for the
                            // config file, etc
        bool close = true;  // close stdin, stdout, stderr
        if (daemon(!chdir, !close) != 0) {
            PANIC("Call to daemon() failed: %s", strerror(errno));
        }
        int pid = getpid();
        Core::Debug::processName = Core::StringUtil::format("%d", pid);
        NOTICE("Detached as daemon with pid %d", pid);
    }

    // Write PID file, removed upon destruction
    PidFile pidFile(options.pidFilename);
    pidFile.writePid(getpid());

    // Initialize and run Globals.
    Server::Globals globals;
    globals.config.readFile(options.configFilename.c_str());
    globals.init(options.serverId);
    if (options.bootstrap) {
        globals.raft->bootstrapConfiguration();
        NOTICE("Done bootstrapping configuration. Exiting.");
    } else {
        globals.run();
    }

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
