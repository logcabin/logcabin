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

#ifndef LOGCABIN_STORAGE_SNAPSHOTFILE_H
#define LOGCABIN_STORAGE_SNAPSHOTFILE_H

namespace LogCabin {
namespace Storage {
namespace SnapshotFile {

// TODO(ongaro): protobuf::io uses 32-bit integer file offsets, but snapshots
// can be larger than that. A lot of this is going to break for files larger
// than 2 or 4GB.

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
    /// Return the size in bytes for the file.
    uint64_t getSizeBytes();
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
     *      The directory in which to create the snapshot (in a file called
     *      "snapshot").
     * TODO(ongaro): what if it can't be written?
     */
    explicit Writer(const Storage::FilesystemUtil::File& parentDir);
    /**
     * Destructor.
     * If the file hasn't been explicitly saved or discarded, prints a warning
     * and leaves the file around for manual inspection.
     */
    ~Writer();
    /**
     * Throw away the file.
     * If you call this after the file has been closed, it will PANIC.
     */
    void discard();
    /**
     * Flush changes just down to the operating system's buffer cache.
     * Leave the file open for additional writes.
     * If you call this after the file has been closed, it will PANIC.
     *
     * This is useful when forking child processes to write to the file.
     * The correct procedure for that is:
     *  0. write stuff
     *  1. call flushToOS()
     *  2. fork
     *  3. child process: write stuff
     *  4. child process: call flushToOS()
     *  5. child process: call _exit()
     *  6. parent process: write stuff
     *  7. parent process: call save()
     */
    void flushToOS();
    /**
     * Flush changes all the way down to the disk and close the file.
     * If you call this after the file has been closed, it will PANIC.
     * \return
     *      Size in bytes of the file
     */
    uint64_t save();
    /// Returns the output stream to write into.
    google::protobuf::io::CodedOutputStream& getStream();
  private:
    /// A handle to the directory containing the snapshot. Used for renameat on
    /// close.
    Storage::FilesystemUtil::File parentDir;
    /// The temporary name of 'file' before it is closed.
    std::string stagingName;
    /// Wraps the raw file descriptor; in charge of closing it when done.
    Storage::FilesystemUtil::File file;
    /// Actually writes the file to disk.
    std::unique_ptr<google::protobuf::io::FileOutputStream> fileStream;
    /// Generates the raw bytes for the 'fileStream'.
    std::unique_ptr<google::protobuf::io::CodedOutputStream> codedStream;
};

} // namespace LogCabin::Storage::SnapshotFile
} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_SNAPSHOTFILE_H */
