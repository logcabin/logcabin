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
#include "Server/SnapshotFile.h"

namespace LogCabin {
namespace Server {
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

google::protobuf::io::CodedInputStream&
Reader::getStream()
{
     return *codedStream;
}

Writer::Writer(const FilesystemUtil::File& parentDir)
    : file()
    , fileStream()
    , codedStream()
{
    file = FilesystemUtil::openFile(parentDir, "snapshot", O_WRONLY|O_CREAT);
    fileStream.reset(new google::protobuf::io::FileOutputStream(file.fd));
    codedStream.reset(
            new google::protobuf::io::CodedOutputStream(fileStream.get()));
}

Writer::~Writer()
{
    // TODO(ongaro): sprinkle some fsyncs in here
}

void
Writer::close()
{
    // TODO(ongaro): sprinkle some fsyncs in here
    codedStream.reset();
    fileStream->Flush();
    fileStream.reset();
    file.close();
}

google::protobuf::io::CodedOutputStream&
Writer::getStream()
{
     return *codedStream;
}

} // namespace LogCabin::Server::SnapshotFile
} // namespace LogCabin::Server
} // namespace LogCabin
