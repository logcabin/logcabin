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

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "build/Protocol/Raft.pb.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "RPC/Buffer.h"
#include "RPC/ProtoBuf.h"
#include "Storage/FilesystemUtil.h"
#include "Server/RaftLog.h"

namespace LogCabin {
namespace Server {
namespace RaftConsensusInternal {

namespace FilesystemUtil = Storage::FilesystemUtil;

namespace {
bool
fileToProto(const std::string& path, google::protobuf::Message& out)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        return false;
    else
        close(fd);
    FilesystemUtil::FileContents file(path);
#if BINARY_FORMAT
    RPC::Buffer contents(const_cast<void*>(file.get(0, file.getFileLength())),
                         file.getFileLength(), NULL);
    return RPC::ProtoBuf::parse(contents, out);
#else
    std::string contents(
            static_cast<const char*>(file.get(0, file.getFileLength())),
            file.getFileLength());
    Core::ProtoBuf::Internal::fromString(contents, out);
    return true;
#endif
}

void
protoToFile(const google::protobuf::Message& in,
            const std::string& path)
{
#if BINARY_FORMAT
    RPC::Buffer contents;
    RPC::ProtoBuf::serialize(in, contents);
#else
    std::string contents(Core::ProtoBuf::dumpString(in, false));
#endif
    int fd = open(path.c_str(), O_CREAT|O_WRONLY, 0600);
    // TODO(ongaro): error?
#if BINARY_FORMAT
    FilesystemUtil::write(fd, contents.getData(), contents.getLength());
#else
    FilesystemUtil::write(fd, contents.data(), uint32_t(contents.length()));
#endif
    // TODO(ongaro): error?
    close(fd);
    // TODO(ongaro): error?
}
}

////////// Log //////////

Log::Log(const std::string& path)
    : path(path)
    , metadata()
    , entries()
{
    if (!path.empty()) {
        if (mkdir(path.c_str(), 0755) == 0) {
            FilesystemUtil::syncDir(path + "/..");
        } else {
            if (errno != EEXIST) {
                PANIC("Failed to create directory for FilesystemLog:"
                      " mkdir(%s) failed: %s", path.c_str(), strerror(errno));
            }
        }

        bool success = fileToProto(path + "/metadata", metadata);
        if (!success)
            WARNING("Error reading metadata");

        std::vector<uint64_t> entryIds = getEntryIds();
        std::sort(entryIds.begin(), entryIds.end());
        for (auto it = entryIds.begin(); it != entryIds.end(); ++it) {
            std::string entryPath = Core::StringUtil::format("%s/%016lx",
                                                             path.c_str(),
                                                             *it);
            uint64_t entryId = append(read(entryPath));
            assert(entryId == *it);
        }
    }
}

std::vector<uint64_t>
Log::getEntryIds() const
{
    std::vector<std::string> filenames = FilesystemUtil::ls(path);
    std::vector<uint64_t> entryIds;
    for (auto it = filenames.begin(); it != filenames.end(); ++it) {
        const std::string& filename = *it;
        if (filename == "metadata")
            continue;
        uint64_t entryId;
        unsigned bytesConsumed;
        int matched = sscanf(filename.c_str(), "%016lx%n", // NOLINT
                             &entryId, &bytesConsumed);
        if (matched != 1 || bytesConsumed != filename.length()) {
            WARNING("%s doesn't look like a valid entry ID (from %s)",
                 filename.c_str(),
                 (path + "/" + filename).c_str());
            continue;
        }
        entryIds.push_back(entryId);
    }
    return entryIds;
}

Log::Entry
Log::read(const std::string& entryPath) const
{
    Protocol::Raft::Entry entry;
    bool success = fileToProto(entryPath, entry);
    assert(success);
    return entry;
}

uint64_t
Log::append(const Entry& entry)
{
    entries.push_back(entry);
    uint64_t entryId = entries.size();
    if (!path.empty()) {
        protoToFile(entry, Core::StringUtil::format("%s/%016lx",
                                                    path.c_str(),
                                                    entryId));
    }
    return entryId;
}

uint64_t
Log::getBeginLastTermId() const
{
    uint64_t entryId = getLastLogId();
    uint64_t lastLogTerm = getTerm(entryId);
    if (entryId == 0)
        return 0;
    while (true) {
        --entryId;
        if (getTerm(entryId) != lastLogTerm)
            return entryId + 1;
    }
}

const Log::Entry&
Log::getEntry(uint64_t entryId) const
{
    uint64_t index = entryId - 1;
    return entries.at(index);
}

uint64_t
Log::getLastLogId() const
{
    return entries.size();
}


uint64_t
Log::getTerm(uint64_t entryId) const
{
    uint64_t index = entryId - 1; // may roll over to ~0UL
    if (index >= entries.size())
        return 0;
    return entries.at(index).term();
}

void
Log::truncate(uint64_t lastEntryId)
{
    if (lastEntryId < entries.size()) {
        if (!path.empty()) {
            for (auto entryId = getLastLogId();
                 entryId > lastEntryId;
                 --entryId) {
                FilesystemUtil::remove(Core::StringUtil::format("%s/%016lx",
                                                                 path.c_str(),
                                                                 entryId));
            }
        }
        entries.resize(lastEntryId);
    }
}

void
Log::updateMetadata()
{
    if (!path.empty()) {
        protoToFile(metadata, path + "/metadata");
    }
}

// TODO(ongaro): worry about corruption
// TODO(ongaro): worry about fail-stop

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin
