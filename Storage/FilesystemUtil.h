/* Copyright (c) 2011-2012 Stanford University
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

/**
 * \file
 * Contains utilities for working with the filesystem.
 */

#include <cinttypes>
#include <string>
#include <vector>

#ifndef LOGCABIN_STORAGE_FILESYSTEMUTIL_H
#define LOGCABIN_STORAGE_FILESYSTEMUTIL_H

namespace LogCabin {
namespace Storage {
namespace FilesystemUtil {

/**
 * List the contents of a directory.
 * Panics if the 'path' is not a directory.
 * \param path
 *      The path to the directory whose contents to list.
 * \return
 *      The names of the directory entries in the order returned by readdir.
 *      The caller will often want to prepend 'path' and a slash to these to
 *      form a path.
 */
std::vector<std::string> ls(const std::string& path);

/**
 * Remove the file or directory at path.
 * If path is a directory, its contents will also be removed.
 * If path does not exist, this returns without an error.
 * This operation is not atomic but is idempotent.
 */
void remove(const std::string& path);

/**
 * Open a directory, fsync it, and close it. This is useful to fsync a
 * directory after creating a file or directory within it.
 */
void syncDir(const std::string& path);

/**
 * Return a path suitable for use as a temporary file or directory that is
 * likely to not exist.
 * \warning
 *      Be aware of the race condition that other processes may take this name
 *      between this call and the caller creating the file or directory.
 */
std::string tmpnam();

/**
 * A wrapper around write that retries interrupted calls.
 * \param fildes
 *      The file handle on which to write data.
 * \param data
 *      A pointer to the data to write.
 * \param dataLen
 *      The number of bytes of 'data' to write.
 * \return
 *      Either -1 with errno set, or the number of bytes requested to write.
 *      This wrapper will never return -1 with errno set to EINTR.
 */
ssize_t
write(int fildes, const void* data, uint32_t dataLen);

/**
 * A wrapper around write that retries interrupted calls.
 * \param fildes
 *      The file handle on which to write data.
 * \param data
 *      An I/O vector of data to write (pointer, length pairs).
 * \return
 *      Either -1 with errno set, or the number of bytes requested to write.
 *      This wrapper will never return -1 with errno set to EINTR.
 */
ssize_t
write(int fildes,
      std::initializer_list<std::pair<const void*, uint32_t>> data);

/**
 * Provides random access to a file.
 * This implementation currently works by mmaping the file and working from the
 * in-memory copy.
 */
class FileContents {
  public:
    /**
     * Constructor.
     * If the file at 'path' can't be opened, this will PANIC.
     * \param path
     *      The filesystem path of the file to read.
     */
    explicit FileContents(const std::string& path);

    /// Destructor.
    ~FileContents();

    /**
     * Return the length of the file.
     */
    uint32_t getFileLength() { return fileLen; }

    /**
     * Copy some number of bytes of the file into a user-supplied buffer.
     *
     * If there are not enough bytes in the file, this will PANIC. See
     * copyPartial if that's not what you want.
     *
     * \param offset
     *      The number of bytes into the file at which to start copying.
     * \param[out] buf
     *      The destination buffer to copy into.
     * \param length
     *      The number of bytes to copy.
     */
    void copy(uint32_t offset, void* buf, uint32_t length);

    /**
     * Copy up to some number of bytes of the file into a user-supplied buffer.
     *
     * \param offset
     *      The number of bytes into the file at which to start copying.
     * \param[out] buf
     *      The destination buffer to copy into.
     * \param maxLength
     *      The maximum number of bytes to copy.
     * \return
     *      The number of bytes copied. This can be fewer than maxLength if the
     *      file ended first.
     */
    uint32_t copyPartial(uint32_t offset, void* buf, uint32_t maxLength);

    /**
     * Get a pointer to a region of the file.
     *
     * If there are not enough bytes in the file, this will PANIC.
     *
     * \tparam T
     *      The return type is casted to a pointer of T.
     * \param offset
     *      The number of bytes into the file at which to return the pointer.
     * \param length
     *      The number of bytes that must be valid after the offset.
     * \return
     *      A pointer to a buffer containing the 'length' bytes starting at
     *      'offset' in the file. The caller may not modify this buffer. The
     *      returned buffer will remain valid until this object is destroyed.
     */
    template<typename T = void>
    const T*
    get(uint32_t offset, uint32_t length) {
        return reinterpret_cast<const T*>(getHelper(offset, length));
    }

  private:
    /// Used internally by get().
    const void* getHelper(uint32_t offset, uint32_t length);
    /// See constructor.
    const std::string path;
    /// The file descriptor returned by open().
    int fd;
    /// The number of bytes in the file.
    uint32_t fileLen;
    /// The value returned by mmap().
    const void* map;
    FileContents(const FileContents&) = delete;
    FileContents& operator=(const FileContents&) = delete;
};

} // namespace LogCabin::Storage::FilesystemUtil
} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_FILESYSTEMUTIL_H */
