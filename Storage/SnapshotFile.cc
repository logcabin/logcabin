/* Copyright (c) 2013 Stanford University
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
#include "Storage/SnapshotFile.h"

namespace LogCabin {
namespace Storage {
namespace SnapshotFile {

namespace FilesystemUtil = Storage::FilesystemUtil;

Reader::Reader(const FilesystemUtil::File& parentDir)
    : file()
    , fileStream()
    , codedStream()
{
    file = FilesystemUtil::tryOpenFile(parentDir, "snapshot", O_RDONLY);
    if (file.fd < 0) {
        throw std::runtime_error(
                Core::StringUtil::format("Snapshot file not found in %s",
                                         parentDir.path.c_str()));
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

google::protobuf::io::CodedInputStream&
Reader::getStream()
{
     return *codedStream;
}

Writer::Writer(const FilesystemUtil::File& parentDir)
    : parentDir(FilesystemUtil::dup(parentDir))
    , stagingName()
    , file()
    , fileStream()
    , codedStream()
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
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
    if (file.fd >= 0)
        WARNING("Leaving behind partial snapshot %s", file.path.c_str());
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

google::protobuf::io::CodedOutputStream&
Writer::getStream()
{
     return *codedStream;
}

} // namespace LogCabin::Storage::SnapshotFile
} // namespace LogCabin::Storage
} // namespace LogCabin
