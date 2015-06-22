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
#include <iterator>
#include <sstream>

#include <LogCabin/Client.h>
#include <LogCabin/Debug.h>
#include <LogCabin/Util.h>

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Tree;

enum class Command {
    MKDIR,
    LIST,
    DUMP,
    RMDIR,
    WRITE,
    READ,
    REMOVE,
};

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , cluster("logcabin:5254")
        , command()
        , condition()
        , dir()
        , path()
        , quiet(false)
        , timeout(LogCabin::Client::Util::parseDuration("0s"))
    {
        while (true) {
            static struct option longOptions[] = {
               {"cluster",  required_argument, NULL, 'c'},
               {"dir",  required_argument, NULL, 'd'},
               {"condition",  required_argument, NULL, 'p'},
               {"quiet",  no_argument, NULL, 'q'},
               {"timeout",  required_argument, NULL, 't'},
               {"help",  no_argument, NULL, 'h'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "c:d:p:t:hq", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'c':
                    cluster = optarg;
                    break;
                case 'd':
                    dir = optarg;
                    break;
                case 'p': {
                    std::istringstream stream(optarg);
                    std::string path;
                    std::string value;
                    std::getline(stream, path, ':');
                    std::getline(stream, value);
                    condition = {path, value};
                    break;
                }
                case 't':
                    timeout = LogCabin::Client::Util::parseDuration(optarg);
                    break;
                case 'h':
                    usage();
                    exit(0);
                case 'q':
                    quiet = true;
                    break;
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

        std::string cmdStr = argv[optind];
        ++optind;
        if (cmdStr == "mkdir") {
            command = Command::MKDIR;
        } else if (cmdStr == "list" || cmdStr == "ls") {
            command = Command::LIST;
        } else if (cmdStr == "dump") {
            command = Command::DUMP;
            path = "/";
        } else if (cmdStr == "rmdir" || cmdStr == "removeDir") {
            command = Command::RMDIR;
        } else if (cmdStr == "write" || cmdStr == "create" ||
                   cmdStr == "set") {
            command = Command::WRITE;
        } else if (cmdStr == "read" || cmdStr == "get") {
            command = Command::READ;
        } else if (cmdStr == "remove" || cmdStr == "rm" ||
                   cmdStr == "removeFile") {
            command = Command::REMOVE;
        } else {
            std::cout << "Unknown command: " << cmdStr << std::endl;
            usage();
            exit(1);
        }

        if (optind < argc) {
            path = argv[optind];
            ++optind;
        }

        if (path.empty()) {
            std::cout << "No path given" << std::endl;
            usage();
            exit(1);
        }
        if (optind < argc) {
            std::cout << "Unexpected positional argument: " << argv[optind]
                      << std::endl;
            usage();
            exit(1);
        }
    }

    void usage() {
        std::cout << "Run various operations on a LogCabin replicated state "
                  << "machine."
                  << std::endl;
        std::cout << std::endl;

        std::cout << "Usage: " << argv[0] << " [options] <command> [<args>]"
                  << std::endl;
        std::cout << std::endl;

        std::cout << "Commands:" << std::endl;
        std::cout
            << "  mkdir <path>    If no directory exists at <path>, create it."
            << std::endl
            << "  list <path>     List keys within directory at <path>. "
            << "Alias: ls."
            << std::endl
            << "  dump [<path>]   Recursively print keys and values within "
            << "directory at <path>."
            << std::endl
            << "                  Defaults to printing all keys and values "
            << "from root of tree."
            << std::endl
            << "  rmdir <path>    Recursively remove directory at <path>, if "
            << "any."
            << std::endl
            << "                  Alias: removedir."
            << std::endl
            << "  write <path>    Set/create value of file at <path> to "
            << "stdin."
            << std::endl
            << "                  Alias: create, set."
            << std::endl
            << "  read <path>     Print value of file at <path>. Alias: get."
            << std::endl
            << "  remove <path>   Remove file at <path>, if any. Alias: rm, "
            << "removefile."
            << std::endl
            << std::endl;

        std::cout << "Options:" << std::endl;
        std::cout
            << "  -c <addresses>, --cluster=<addresses>  "
            << "Network addresses of the LogCabin"
            << std::endl
            << "                                         "
            << "servers, comma-separated"
            << std::endl
            << "                                         "
            << "[default: logcabin:5254]"
            << std::endl

            << "  -d <path>, --dir=<path>        "
            << "Set working directory [default: /]"
            << std::endl

            << "  -h, --help                     "
            << "Print this usage information"
            << std::endl

            << "  -p <pred>, --condition=<pred>  "
            << "Set predicate on the operation of the"
            << std::endl
            << "                                 "
            << "form <path>:<value>, indicating that the key"
            << std::endl
            << "                                 "
            << "at <path> must have the given value."
            << std::endl

            << "  -q, --quiet                    "
            << "Suppress NOTICE messages"
            << std::endl

            << "  -t <time>, --timeout=<time>    "
            << "Set timeout for the operation"
            << std::endl
            << "                                 "
            << "(0 means wait forever) [default: 0s]"
            << std::endl;
    }

    int& argc;
    char**& argv;
    std::string cluster;
    Command command;
    std::pair<std::string, std::string> condition;
    std::string dir;
    std::string path;
    bool quiet;
    uint64_t timeout;
};

/**
 * Depth-first search tree traversal, dumping out contents of all files
 */
void
dumpTree(const Tree& tree, std::string path)
{
    std::cout << path << std::endl;
    std::vector<std::string> children = tree.listDirectoryEx(path);
    for (auto it = children.begin(); it != children.end(); ++it) {
        std::string child = path + *it;
        if (*child.rbegin() == '/') { // directory
            dumpTree(tree, child);
        } else { // file
            std::cout << child << ": " << std::endl;
            std::cout << "    " << tree.readEx(child) << std::endl;
        }
    }
}

std::string
readStdin()
{
    std::cin >> std::noskipws;
    std::istream_iterator<char> it(std::cin);
    std::istream_iterator<char> end;
    std::string results(it, end);
    return results;
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    OptionParser options(argc, argv);

    if (options.quiet) {
        LogCabin::Client::Debug::setLogPolicy({{"", "WARNING"}});
    }

    Cluster cluster(options.cluster);
    Tree tree = cluster.getTree();

    if (options.timeout > 0) {
        if (!options.quiet) {
            std::cout << "Setting timeout to "
                      << options.timeout
                      << " nanoseconds"
                      << std::endl;
        }
        tree.setTimeout(options.timeout);
    }

    if (!options.dir.empty()) {
        if (!options.quiet) {
            std::cout << "Setting working directory to "
                      << options.dir
                      << std::endl;
        }
        tree.setWorkingDirectoryEx(options.dir);
    }

    if (!options.condition.first.empty()) {
        if (!options.quiet) {
            std::cout << "Setting condition that "
                      << options.condition.first
                      << " == "
                      << options.condition.second
                      << std::endl;
        }
        tree.setConditionEx(options.condition.first,
                            options.condition.second);
    }

    std::string& path = options.path;
    switch (options.command) {
        case Command::MKDIR:
            tree.makeDirectoryEx(path);
            break;
        case Command::LIST: {
            std::vector<std::string> keys = tree.listDirectoryEx(path);
            for (auto it = keys.begin(); it != keys.end(); ++it)
                std::cout << *it << std::endl;
            break;
        }
        case Command::DUMP: {
            if (path.empty() || path.at(path.size() - 1) != '/')
                path.append("/");
            dumpTree(tree, path);
            break;
        }
        case Command::RMDIR:
            tree.removeDirectoryEx(path);
            break;
        case Command::WRITE:
            tree.writeEx(path, readStdin());
            break;
        case Command::READ: {
            std::string contents = tree.readEx(path);
            std::cout << contents;
            if (contents.empty() || contents.at(contents.size() - 1) != '\n')
                std::cout << std::endl;
            else
                std::cout.flush();
            break;
        }
        case Command::REMOVE:
            tree.removeFileEx(path);
            break;
    }
    return 0;
}

