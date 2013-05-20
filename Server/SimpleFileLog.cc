/* Copyright (c) 2012-2013 Stanford University
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
#include "Core/Checksum.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "Core/Util.h"
#include "RPC/Buffer.h"
#include "RPC/ProtoBuf.h"
#include "Storage/FilesystemUtil.h"
#include "Server/SimpleFileLog.h"

namespace LogCabin {
namespace Server {
namespace RaftConsensusInternal {

namespace FilesystemUtil = Storage::FilesystemUtil;
using FilesystemUtil::File;
using Core::StringUtil::format;

namespace {
std::string
fileToProto(const File& dir, const std::string& path,
            google::protobuf::Message& out)
{
    FilesystemUtil::File file =
        FilesystemUtil::tryOpenFile(dir, path, O_RDONLY);
    if (file.fd == -1) {
        return format("Could not open %s/%s: %s",
                      dir.path.c_str(), path.c_str(), strerror(errno));
    }
    FilesystemUtil::FileContents reader(file);

    char checksum[Core::Checksum::MAX_LENGTH];
    uint32_t bytesRead = reader.copyPartial(0, checksum, sizeof32(checksum));
    uint32_t checksumBytes = Core::Checksum::length(checksum, bytesRead);
    if (checksumBytes == 0)
        return format("File %s missing checksum", file.path.c_str());

    uint32_t dataLen = reader.getFileLength() - checksumBytes;
    const void* data = reader.get(checksumBytes, dataLen);
    std::string error = Core::Checksum::verify(checksum, data, dataLen);
    if (!error.empty()) {
        return format("Checksum verification failure on %s: %s",
                      file.path.c_str(), error.c_str());
    }

#if BINARY_FORMAT
    RPC::Buffer contents(const_cast<void*>(data), dataLen, NULL);
    if (!RPC::ProtoBuf::parse(contents, out))
        return format("Failed to parse protobuf in %s", file.path.c_str());
#else
    std::string contents(static_cast<const char*>(data), dataLen);
    Core::ProtoBuf::Internal::fromString(contents, out);
#endif
    return "";
}

void
protoToFile(const google::protobuf::Message& in,
            const File& dir, const std::string& path)
{
    FilesystemUtil::File file =
        FilesystemUtil::openFile(dir, path, O_CREAT|O_WRONLY|O_TRUNC);
    const void* data = NULL;
    uint32_t len = 0;
#if BINARY_FORMAT
    RPC::Buffer contents;
    RPC::ProtoBuf::serialize(in, contents);
    data = contents.getData();
    len = contents.getLenhgt();
#else
    std::string contents(Core::ProtoBuf::dumpString(in));
    contents = "\n" + contents;
    data = contents.data();
    len = uint32_t(contents.length());
#endif
    char checksum[Core::Checksum::MAX_LENGTH];
    uint32_t checksumLen = Core::Checksum::calculate("SHA-1",
                                                     data, len,
                                                     checksum);

    ssize_t written = FilesystemUtil::write(file.fd, {
        {checksum, checksumLen},
        {data, len},
    });
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

std::string
SimpleFileLog::readMetadata(const std::string& filename,
                            SimpleFileLogMetadata::Metadata& metadata) const
{
    std::string error = fileToProto(dir, filename, metadata);
    if (!error.empty())
        return error;
    for (uint64_t entryId = metadata.entries_start();
         entryId <= metadata.entries_end();
         ++entryId) {
        Protocol::Raft::Entry entry;
        error = fileToProto(dir, format("%016lx", entryId), entry);
        if (!error.empty()) {
            return format("Could not parse file %s/%016lx: %s",
                          dir.path.c_str(), entryId, error.c_str());
        }
    }
    return "";
}

SimpleFileLog::SimpleFileLog(const FilesystemUtil::File& parentDir)
    : metadata()
    , dir(FilesystemUtil::openDir(parentDir, "log"))
    , lostAndFound(FilesystemUtil::openDir(dir, "unknown"))
{
    std::vector<uint64_t> fsEntryIds = getEntryIds();

    SimpleFileLogMetadata::Metadata metadata1;
    SimpleFileLogMetadata::Metadata metadata2;
    std::string error1 = readMetadata("metadata1", metadata1);
    std::string error2 = readMetadata("metadata2", metadata2);
    if (error1.empty() && error2.empty()) {
        if (metadata1.version() > metadata2.version())
            metadata = metadata1;
        else
            metadata = metadata2;
    } else if (error1.empty()) {
        metadata = metadata1;
    } else if (error2.empty()) {
        metadata = metadata2;
    } else {
        // Brand new servers won't have metadata.
        WARNING("Error reading metadata1: %s", error1.c_str());
        WARNING("Error reading metadata2: %s", error2.c_str());
        if (!fsEntryIds.empty()) {
            PANIC("No readable metadata file but found entries in %s",
                  dir.path.c_str());
        }
        metadata.set_entries_start(1);
        metadata.set_entries_end(0);
    }

    std::vector<uint64_t> found;
    for (auto it = fsEntryIds.begin(); it != fsEntryIds.end(); ++it) {
        if (*it < metadata.entries_start() || *it > metadata.entries_end())
            found.push_back(*it);
    }

    std::string time;
    {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        time = format("%010lu.%06lu", now.tv_sec, now.tv_nsec / 1000);
    }
    for (auto it = found.begin(); it != found.end(); ++it) {
        uint64_t entryId = *it;
        std::string oldName = format("%016lx", entryId);
        std::string newName = format("%s-%016lx", time.c_str(), entryId);
        WARNING("Moving extraneous file %s/%s to %s/%s",
                dir.path.c_str(), oldName.c_str(),
                lostAndFound.path.c_str(), newName.c_str());
        FilesystemUtil::rename(dir, oldName,
                               lostAndFound, newName);
        FilesystemUtil::fsync(lostAndFound);
        FilesystemUtil::fsync(dir);
    }

    Log::truncatePrefix(metadata.entries_start());
    for (uint64_t id = metadata.entries_start();
         id <= metadata.entries_end();
         ++id) {
        uint64_t entryId = Log::append(read(format("%016lx", id)));
        assert(entryId == id);
    }

    Log::metadata = metadata.raft_metadata();
    // Write both metadata files
    updateMetadata();
    updateMetadata();
}

SimpleFileLog::~SimpleFileLog()
{
}

uint64_t
SimpleFileLog::append(const Entry& entry)
{
    uint64_t entryId = Log::append(entry);
    protoToFile(entry, dir, format("%016lx", entryId));
    updateMetadata();
    return entryId;
}

void
SimpleFileLog::truncatePrefix(uint64_t firstEntryId)
{
    uint64_t old = getLogStartIndex();
    Log::truncatePrefix(firstEntryId);
    // update metadata before removing files in case of interruption
    updateMetadata();
    for (uint64_t entryId = old; entryId < getLogStartIndex(); ++entryId)
        FilesystemUtil::removeFile(dir, format("%016lx", entryId));
    // fsync(dir) not needed because of metadata
}

void
SimpleFileLog::truncateSuffix(uint64_t lastEntryId)
{
    uint64_t old = getLastLogIndex();
    Log::truncateSuffix(lastEntryId);
    // update metadata before removing files in case of interruption
    updateMetadata();
    for (uint64_t entryId = old; entryId > getLastLogIndex(); --entryId)
        FilesystemUtil::removeFile(dir, format("%016lx", entryId));
    // fsync(dir) not needed because of metadata
}

void
SimpleFileLog::updateMetadata()
{
    Log::updateMetadata();
    *metadata.mutable_raft_metadata() = Log::metadata;
    metadata.set_entries_start(Log::getLogStartIndex());
    metadata.set_entries_end(Log::getLastLogIndex());
    metadata.set_version(metadata.version() + 1);
    if (metadata.version() % 2 == 1) {
        protoToFile(metadata, dir, "metadata1");
    } else {
        protoToFile(metadata, dir, "metadata2");
    }
}

std::vector<uint64_t>
SimpleFileLog::getEntryIds() const
{
    std::vector<std::string> filenames = FilesystemUtil::ls(dir);
    std::vector<uint64_t> entryIds;
    for (auto it = filenames.begin(); it != filenames.end(); ++it) {
        const std::string& filename = *it;
        if (filename == "metadata1" ||
            filename == "metadata2" ||
            filename == "unknown") {
            continue;
        }
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
    std::string error = fileToProto(dir, entryPath, entry);
    if (!error.empty())
        PANIC("Could not parse file: %s", error.c_str());
    return entry;
}

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin
