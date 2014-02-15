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
    uint64_t bytesRead = reader.copyPartial(0, checksum, sizeof(checksum));
    uint32_t checksumBytes = Core::Checksum::length(checksum,
                                                    uint32_t(bytesRead));
    if (checksumBytes == 0)
        return format("File %s missing checksum", file.path.c_str());

    uint64_t dataLen = reader.getFileLength() - checksumBytes;
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

FilesystemUtil::File
protoToFile(const google::protobuf::Message& in,
            const File& dir, const std::string& path)
{
    FilesystemUtil::File file =
        FilesystemUtil::openFile(dir, path, O_CREAT|O_WRONLY|O_TRUNC);
    const void* data = NULL;
    uint64_t len = 0;
#if BINARY_FORMAT
    RPC::Buffer contents;
    RPC::ProtoBuf::serialize(in, contents);
    data = contents.getData();
    len = contents.getLenhgt();
#else
    std::string contents(Core::ProtoBuf::dumpString(in));
    contents = "\n" + contents;
    data = contents.data();
    len = uint64_t(contents.length());
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

    return file;
}
}

////////// SimpleFileLog::Sync //////////
SimpleFileLog::Sync::Sync(std::unique_ptr<Log::Sync> memSync)
    : Log::Sync(memSync->firstEntryId, memSync->lastEntryId)
    , mutex()
    , fds()
{
}

std::string
SimpleFileLog::Sync::wait()
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    std::string error;
    for (auto it = fds.begin(); it != fds.end(); ++it) {
        if (::fsync(it->first) != 0) {
            error += format("Could not fsync fd %d for "
                            "log entries %lu-%lu: %s. ",
                            it->first, firstEntryId, lastEntryId,
                            strerror(errno));
        } else if (it->second && ::close(it->first) != 0) {
            error += format("Could not close fd %d for "
                            "log entries %lu-%lu: %s. ",
                            it->first, firstEntryId, lastEntryId,
                            strerror(errno));
        }
    }
    fds.clear();
    return error;
}

////////// SimpleFileLog //////////

std::string
SimpleFileLog::readMetadata(const std::string& filename,
                            SimpleFileLogMetadata::Metadata& metadata) const
{
    std::string error = fileToProto(dir, filename, metadata);
    if (!error.empty())
        return error;
    return "";
}

SimpleFileLog::SimpleFileLog(const FilesystemUtil::File& parentDir)
    : memoryLog()
    , metadata()
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

    memoryLog.truncatePrefix(metadata.entries_start());
    for (uint64_t id = metadata.entries_start();
         id <= metadata.entries_end();
         ++id) {
        memoryLog.append(read(format("%016lx", id)))->wait();
    }

    Log::metadata = metadata.raft_metadata();
    // Write both metadata files
    updateMetadata();
    updateMetadata();
}

SimpleFileLog::~SimpleFileLog()
{
}

std::unique_ptr<Log::Sync>
SimpleFileLog::append(const Entry& entry)
{
    std::unique_ptr<SimpleFileLog::Sync> sync(
        new SimpleFileLog::Sync(memoryLog.append(entry)));
    uint64_t entryId = sync->firstEntryId;
    FilesystemUtil::File file =
        protoToFile(entry, dir, format("%016lx", entryId));
    FilesystemUtil::File mdfile = updateMetadataCallerSync();
    sync->fds.push_back({file.release(), true});
    sync->fds.push_back({dir.fd, false});
    sync->fds.push_back({mdfile.release(), true});
    return std::move(sync);
}

void
SimpleFileLog::truncatePrefix(uint64_t firstEntryId)
{
    uint64_t old = getLogStartIndex();
    memoryLog.truncatePrefix(firstEntryId);
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
    memoryLog.truncateSuffix(lastEntryId);
    // update metadata before removing files in case of interruption
    updateMetadata();
    for (uint64_t entryId = old; entryId > getLastLogIndex(); --entryId)
        FilesystemUtil::removeFile(dir, format("%016lx", entryId));
    // fsync(dir) not needed because of metadata
}

const SimpleFileLog::Entry&
SimpleFileLog::getEntry(uint64_t i) const
{
    return memoryLog.getEntry(i);
}

uint64_t
SimpleFileLog::getLogStartIndex() const
{
    return memoryLog.getLogStartIndex();
}

uint64_t
SimpleFileLog::getLastLogIndex() const
{
    return memoryLog.getLastLogIndex();
}

uint64_t
SimpleFileLog::getSizeBytes() const
{
    return memoryLog.getSizeBytes();
}

void
SimpleFileLog::updateMetadata()
{
    // sync file to disk
    FilesystemUtil::fsync(updateMetadataCallerSync());
    // sync directory entry to disk (needed if we created file)
    FilesystemUtil::fsync(dir);
}

FilesystemUtil::File
SimpleFileLog::updateMetadataCallerSync()
{
    *metadata.mutable_raft_metadata() = Log::metadata;
    metadata.set_entries_start(memoryLog.getLogStartIndex());
    metadata.set_entries_end(memoryLog.getLastLogIndex());
    metadata.set_version(metadata.version() + 1);
    if (metadata.version() % 2 == 1) {
        return protoToFile(metadata, dir, "metadata1");
    } else {
        return protoToFile(metadata, dir, "metadata2");
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
