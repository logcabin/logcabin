/* Copyright (c) 2013 Stanford University
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Core/Time.h"
#include "Storage/Layout.h"
#include "Storage/SnapshotFile.h"

namespace LogCabin {
namespace Storage {
namespace SnapshotFile {

namespace FilesystemUtil = Storage::FilesystemUtil;

void
discardPartialSnapshots(const Storage::Layout& layout)
{
    std::vector<std::string> files = FilesystemUtil::ls(layout.snapshotDir);
    for (auto it = files.begin(); it != files.end(); ++it) {
        const std::string& filename = *it;
        if (Core::StringUtil::startsWith(filename, "partial")) {
            NOTICE("Removing incomplete snapshot %s. This was probably being "
                   "written when the server crashed.",
                   filename.c_str());
            FilesystemUtil::removeFile(layout.snapshotDir, filename);
        }
    }
}

Reader::Reader(const Storage::Layout& storageLayout)
    : file()
    , fileStream()
    , codedStream()
    , bytesRead(0)
{
    file = FilesystemUtil::tryOpenFile(storageLayout.snapshotDir,
                                       "snapshot",
                                       O_RDONLY);
    if (file.fd < 0) {
        throw std::runtime_error(Core::StringUtil::format(
                "Snapshot file not found in %s",
                storageLayout.snapshotDir.path.c_str()));
    }
    fileStream.reset(new google::protobuf::io::FileInputStream(file.fd));
    codedStream.reset(
            new google::protobuf::io::CodedInputStream(fileStream.get()));
}

Reader::~Reader()
{
}

uint64_t
Reader::getSizeBytes()
{
    return FilesystemUtil::getSize(file);
}


uint64_t
Reader::getBytesRead() const
{
    return bytesRead;
}

bool
Reader::readMessage(google::protobuf::Message& message)
{
    auto& stream = *codedStream;
    bytesRead += 4;
    uint32_t numBytes = 0;
    bool ok = stream.ReadLittleEndian32(&numBytes);
    if (!ok)
        return false;
    auto limit = stream.PushLimit(numBytes);
    ok = message.MergePartialFromCodedStream(&stream);
    stream.PopLimit(limit);
    bytesRead += numBytes;
    return ok;
}

uint64_t
Reader::readRaw(void* data, uint64_t length)
{
    length = std::min(length, getSizeBytes() - getBytesRead());
    if (codedStream->ReadRaw(data, int(length))) {
        bytesRead += length;
        return length;
    } else {
        return 0;
    }
}

Writer::Writer(const Storage::Layout& storageLayout)
    : parentDir(FilesystemUtil::dup(storageLayout.snapshotDir))
    , stagingName()
    , file()
    , fileStream()
    , codedStream()
{
    struct timespec now =
        Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    stagingName = Core::StringUtil::format("partial.%010lu.%06lu",
                                           now.tv_sec, now.tv_nsec / 1000);
    file = FilesystemUtil::openFile(parentDir, stagingName,
                                    O_WRONLY|O_CREAT|O_EXCL);
    fileStream.reset(new google::protobuf::io::FileOutputStream(file.fd));
    codedStream.reset(
            new google::protobuf::io::CodedOutputStream(fileStream.get()));
}

Writer::~Writer()
{
    if (file.fd >= 0) {
        WARNING("Discarding partial snapshot %s", file.path.c_str());
        discard();
    }
}

void
Writer::discard()
{
    if (file.fd < 0)
        PANIC("File already closed");
    FilesystemUtil::removeFile(parentDir, stagingName);
    codedStream.reset();
    fileStream.reset();
    file.close();
}

void
Writer::flushToOS()
{
    if (file.fd < 0)
        PANIC("File already closed");
    // 1. Destroy the old CodedOutputStream.
    codedStream.reset();
    // 2. Flush the FileOutputStream.
    fileStream->Flush();
    // 3. Construct the new CodedOutputStream.
    codedStream.reset(
            new google::protobuf::io::CodedOutputStream(fileStream.get()));
}

uint64_t
Writer::save()
{
    if (file.fd < 0)
        PANIC("File already closed");
    codedStream.reset();
    fileStream->Flush();
    fileStream.reset();
    FilesystemUtil::fsync(file);
    uint64_t fileSize = FilesystemUtil::getSize(file);
    file.close();
    FilesystemUtil::rename(parentDir, stagingName,
                           parentDir, "snapshot");
    FilesystemUtil::fsync(parentDir);
    return fileSize;
}

uint64_t
Writer::getBytesWritten() const
{
    return uint64_t(codedStream->ByteCount());
}

void
Writer::writeMessage(const google::protobuf::Message& message)
{
    int size = message.ByteSize();
    codedStream->WriteLittleEndian32(size);
    message.SerializeWithCachedSizes(codedStream.get());
}

void
Writer::writeRaw(const void* data, uint64_t length)
{
    codedStream->WriteRaw(data, int(length));
}

} // namespace LogCabin::Storage::SnapshotFile
} // namespace LogCabin::Storage
} // namespace LogCabin
