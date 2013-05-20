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

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <memory>
#include <stdexcept>
#include <string>

#include "Storage/FilesystemUtil.h"

#ifndef LOGCABIN_SERVER_SNAPSHOTFILE_H
#define LOGCABIN_SERVER_SNAPSHOTFILE_H

namespace LogCabin {
namespace Server {
namespace SnapshotFile {

/**
 * Assists in reading snapshot files from the local filesystem.
 */
class Reader {
  public:
    /**
     * Constructor.
     * \param parentDir
     *      The directory in which to find the snapshot (in a file called
     *      "snapshot").
     * \throw std::runtime_error
     *      If the file can't be found.
     */
    explicit Reader(const Storage::FilesystemUtil::File& parentDir);
    /// Destructor.
    ~Reader();
    /// Returns the input stream to read from.
    google::protobuf::io::CodedInputStream& getStream();
  private:
    /// Wraps the raw file descriptor; in charge of closing it when done.
    Storage::FilesystemUtil::File file;
    /// Actually reads the file from disk.
    std::unique_ptr<google::protobuf::io::FileInputStream> fileStream;
    /// Interprets the raw bytes from the 'fileStream'.
    std::unique_ptr<google::protobuf::io::CodedInputStream> codedStream;
};

/**
 * Assists in writing snapshot files to the local filesystem.
 */
class Writer {
  public:
    /**
     * Constructor.
     * \param parentDir
     *      The directory in which to find the snapshot (in a file called
     *      "snapshot").
     * TODO(ongaro): what if it can't be written?
     */
    explicit Writer(const Storage::FilesystemUtil::File& parentDir);
    /// Destructor.
    ~Writer();
    /// Flush and close the file.
    void close();
    /// Returns the output stream to write into.
    google::protobuf::io::CodedOutputStream& getStream();
  private:
    /// Wraps the raw file descriptor; in charge of closing it when done.
    Storage::FilesystemUtil::File file;
    /// Actually writes the file to disk.
    std::unique_ptr<google::protobuf::io::FileOutputStream> fileStream;
    /// Generates the raw bytes for the 'fileStream'.
    std::unique_ptr<google::protobuf::io::CodedOutputStream> codedStream;
};

} // namespace LogCabin::Server::SnapshotFile
} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_SNAPSHOTFILE_H */
