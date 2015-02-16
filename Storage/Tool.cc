/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014-2015 Diego Ongaro
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
#include <sys/file.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <string>

#include "build/Server/SnapshotMetadata.pb.h"
#include "build/Server/Sessions.pb.h"
#include "Core/Config.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"
#include "Core/Util.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/LogFactory.h"
#include "Storage/SnapshotFile.h"
#include "Tree/Tree.h"

namespace {

using namespace LogCabin;

/**
 * Parses argv for the main function.
 */
class OptionParser {
  public:
    OptionParser(int& argc, char**& argv)
        : argc(argc)
        , argv(argv)
        , configFilename("logcabin.conf")
        , serverId(0)
    {
        while (true) {
            static struct option longOptions[] = {
               {"config",  required_argument, NULL, 'c'},
               {"help",  no_argument, NULL, 'h'},
               {"id",  required_argument, NULL, 'i'},
               {0, 0, 0, 0}
            };
            int c = getopt_long(argc, argv, "c:hi:", longOptions, NULL);

            // Detect the end of the options.
            if (c == -1)
                break;

            switch (c) {
                case 'h':
                    usage();
                    exit(0);
                case 'c':
                    configFilename = optarg;
                    break;
                case 'i':
                    serverId = uint64_t(atol(optarg));
                    break;
                case '?':
                default:
                    // getopt_long already printed an error message.
                    usage();
                    exit(1);
            }
        }

        // We don't expect any additional command line arguments (not options).
        if (optind != argc || serverId == 0) {
            usage();
            exit(1);
        }
    }

    void usage() {
        std::cout << "Dumps out the contents of LogCabin's storage directory "
                  << "(the log and snapshot)." << std::endl;
        std::cout << std::endl;
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
        std::cout << "Options: " << std::endl;
        std::cout << "  -h, --help            "
                  << "Print this usage information" << std::endl;
         std::cout << "  -c, --config <file>   "
                  << "Specify the configuration file "
                  << "(default: logcabin.conf)" << std::endl;
        std::cout << "  -i, --id <id>         "
                  << "Set server id to <id> (required)" << std::endl;
    }

    int& argc;
    char**& argv;
    std::string configFilename;
    uint64_t serverId;
};

void
dumpTree(const Tree::Tree& tree, const std::string& path = "/")
{
    std::cout << path << std::endl;
    std::vector<std::string> children;
    tree.listDirectory(path, children);
    for (auto it = children.begin();
         it != children.end();
         ++it) {
        if (Core::StringUtil::endsWith(*it, "/")) {
            dumpTree(tree, path + *it);
        } else {
            std::string contents;
            tree.read(path + *it, contents);
            std::cout << path << *it << " : " << contents << std::endl;
        }
    }
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    using namespace LogCabin;
    Core::Util::Finally _(google::protobuf::ShutdownProtobufLibrary);

    Core::ThreadId::setName("main");

    // Parse command line args.
    OptionParser options(argc, argv);

    NOTICE("Using config file %s", options.configFilename.c_str());
    Core::Config config;
    config.readFile(options.configFilename.c_str());

    Storage::FilesystemUtil::File parentDir =
        Storage::FilesystemUtil::openDir(
            config.read<std::string>("storagePath", "storage"));
    Storage::FilesystemUtil::File storageDir =
        Storage::FilesystemUtil::openDir(parentDir,
             Core::StringUtil::format("server%lu", options.serverId));
    std::string error = Storage::FilesystemUtil::tryFlock(storageDir,
                                                          LOCK_EX|LOCK_NB);
    if (!error.empty()) {
        PANIC("Could not lock storage directory. Is LogCabin running? "
              "Error was: %s", error.c_str());
    }

    NOTICE("Opening log at %s", storageDir.path.c_str());
    {
        std::unique_ptr<Storage::Log> log =
            Storage::LogFactory::makeLog(config, storageDir);
        NOTICE("Log contents start");
        std::cout << *log << std::endl;
        NOTICE("Log contents end");
    }

    NOTICE("Reading snapshot at %s", storageDir.path.c_str());

    std::unique_ptr<Storage::SnapshotFile::Reader> reader;
    try {
        reader.reset(new Storage::SnapshotFile::Reader(storageDir));
    } catch (const std::runtime_error& e) { // file not found
        NOTICE("%s", e.what());
    }
    if (reader) {
        google::protobuf::io::CodedInputStream& stream =
            reader->getStream();

        { // read header protobuf from stream
            bool ok = true;
            uint32_t numBytes = 0;
            ok = stream.ReadLittleEndian32(&numBytes);
            if (!ok)
                PANIC("couldn't read snapshot header");
            Server::SnapshotMetadata::Header header;
            auto limit = stream.PushLimit(numBytes);
            ok = header.MergePartialFromCodedStream(&stream);
            stream.PopLimit(limit);
            if (!ok)
                PANIC("couldn't read snapshot header");
            NOTICE("Snapshot header start");
            std::cout << Core::ProtoBuf::dumpString(header) << std::endl;
            NOTICE("Snapshot header end");
        }

        { // read StateMachine sessions from stream
            bool ok = true;
            uint32_t numBytes = 0;
            ok = stream.ReadLittleEndian32(&numBytes);
            if (!ok)
                PANIC("couldn't read snapshot sessions");
            Server::SessionsProto::Sessions sessions;
            auto limit = stream.PushLimit(numBytes);
            ok = sessions.MergePartialFromCodedStream(&stream);
            stream.PopLimit(limit);
            if (!ok)
                PANIC("couldn't read snapshot sessions");
            NOTICE("Snapshot sessions start");
            std::cout << Core::ProtoBuf::dumpString(sessions) << std::endl;
            NOTICE("Snapshot sessions end");
        }

        { // read Tree from stream
            Tree::Tree tree;
            tree.loadSnapshot(stream);
            NOTICE("Snapshot tree start");
            dumpTree(tree);
            NOTICE("Snapshot tree end");
        }
    }

    return 0;
}
