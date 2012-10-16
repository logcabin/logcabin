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
#include "Server/SimpleFileLog.h"

namespace LogCabin {
namespace Server {
namespace RaftConsensusInternal {

namespace FilesystemUtil = Storage::FilesystemUtil;
using FilesystemUtil::File;

namespace {
bool
fileToProto(const File& dir, const std::string& path,
            google::protobuf::Message& out)
{
    FilesystemUtil::File file =
        FilesystemUtil::tryOpenFile(dir, path, O_RDONLY);
    if (file.fd == -1)
        return false;
    FilesystemUtil::FileContents reader(file);
#if BINARY_FORMAT
    RPC::Buffer contents(
            const_cast<void*>(reader.get(0, reader.getFileLength())),
            reader.getFileLength(), NULL);
    return RPC::ProtoBuf::parse(contents, out);
#else
    std::string contents(
            static_cast<const char*>(reader.get(0, reader.getFileLength())),
            reader.getFileLength());
    Core::ProtoBuf::Internal::fromString(contents, out);
    return true;
#endif
}

void
protoToFile(const google::protobuf::Message& in,
            const File& dir, const std::string& path)
{
    FilesystemUtil::File file =
        FilesystemUtil::openFile(dir, path, O_CREAT|O_WRONLY|O_TRUNC);
    ssize_t written;
#if BINARY_FORMAT
    RPC::Buffer contents;
    RPC::ProtoBuf::serialize(in, contents);
    written = FilesystemUtil::write(file.fd, contents.getData(),
                                    contents.getLength());
#else
    std::string contents(Core::ProtoBuf::dumpString(in, false));
    written = FilesystemUtil::write(file.fd, contents.data(),
                                    uint32_t(contents.length()));
#endif
    if (written == -1) {
        PANIC("Failed to write to %s: %s",
              file.path.c_str(), strerror(errno));
    }

    // sync file contents to disk
    FilesystemUtil::fsync(file);

    // sync directory entry to disk (needed if we created fd)
    FilesystemUtil::fsync(dir);
}
}

////////// Log //////////
//

SimpleFileLog::SimpleFileLog(const std::string& path)
    : dir(FilesystemUtil::openDir(path))
{
    bool success = fileToProto(dir, "metadata", metadata);
    if (!success)
        WARNING("Error reading metadata");

    std::vector<uint64_t> entryIds = getEntryIds();
    std::sort(entryIds.begin(), entryIds.end());
    for (auto it = entryIds.begin(); it != entryIds.end(); ++it) {
        std::string entryPath = Core::StringUtil::format("%016lx", *it);
        uint64_t entryId = Log::append(read(entryPath));
        assert(entryId == *it);
    }
}

SimpleFileLog::~SimpleFileLog()
{
}

uint64_t
SimpleFileLog::append(const Entry& entry)
{
    uint64_t entryId = Log::append(entry);
    protoToFile(entry, dir, Core::StringUtil::format("%016lx", entryId));
    return entryId;
}

void
SimpleFileLog::truncate(uint64_t lastEntryId)
{
    for (auto entryId = getLastLogId(); entryId > lastEntryId; --entryId) {
        FilesystemUtil::removeFile(dir, Core::StringUtil::format("%016lx",
                                                                 entryId));
        FilesystemUtil::fsync(dir);
    }
    Log::truncate(lastEntryId);
}

void
SimpleFileLog::updateMetadata()
{
    Log::updateMetadata();
    protoToFile(metadata, dir, "metadata");
}

std::vector<uint64_t>
SimpleFileLog::getEntryIds() const
{
    std::vector<std::string> filenames = FilesystemUtil::ls(dir);
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
                    (dir.path + "/" + filename).c_str());
            continue;
        }
        entryIds.push_back(entryId);
    }
    return entryIds;
}

Log::Entry
SimpleFileLog::read(const std::string& entryPath) const
{
    Protocol::Raft::Entry entry;
    bool success = fileToProto(dir, entryPath, entry);
    assert(success);
    return entry;
}

// TODO(ongaro): worry about corruption
// TODO(ongaro): worry about fail-stop

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin
