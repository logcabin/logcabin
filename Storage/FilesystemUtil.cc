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

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include "include/Common.h"
#include "Core/Debug.h"
#include "Storage/FilesystemUtil.h"

namespace LogCabin {
namespace Storage {
namespace FilesystemUtil {

std::vector<std::string>
ls(const std::string& path)
{
    DIR* dir = opendir(path.c_str());
    if (dir == NULL) {
        PANIC("Could not list contents of %s: %s",
              path.c_str(), strerror(errno));
    }

    std::vector<std::string> contents;
    while (true) {
        struct dirent entry;
        struct dirent* entryp;
        if (readdir_r(dir, &entry, &entryp) != 0) {
            PANIC("readdir(%s) failed: %s",
                  path.c_str(), strerror(errno));
        }
        if (entryp == NULL) // no more entries
            break;
        const std::string name = entry.d_name;
        if (name == "." || name == "..")
            continue;
        contents.push_back(name);
    }

    if (closedir(dir) != 0) {
        WARN("closedir(%s) failed: %s",
             path.c_str(), strerror(errno));
    }

    return contents;
}

void
remove(const std::string& path)
{
    while (true) {
        if (::remove(path.c_str()) == 0)
            return;
        if (errno == ENOENT) {
            return;
        } else if (errno == EEXIST || errno == ENOTEMPTY) {
            std::vector<std::string> children = ls(path);
            for (auto it = children.begin(); it != children.end(); ++it)
                remove(path + "/" + *it);
            continue;
        } else {
            PANIC("Could not remove %s: %s", path.c_str(), strerror(errno));
        }
    }
}

void
syncDir(const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        PANIC("Could not open %s: %s",
              path.c_str(), strerror(errno));
    }
    if (fsync(fd) != 0) {
        PANIC("Could not fsync %s: %s",
              path.c_str(), strerror(errno));
    }
    if (close(fd) != 0) {
        WARN("Failed to close file %s: %s",
             path.c_str(), strerror(errno));
    }
}

std::string
tmpnam()
{
    char d[L_tmpnam];
    std::string path = ::tmpnam(d);
    assert(path != "" && path != "/");
    return path;
}

namespace System {
#if DEBUG
// This is mocked out in some unit tests.
ssize_t (*writev)(int fildes,
                  const struct iovec* iov,
                  int iovcnt) = ::writev;
#else
using ::writev;
#endif
}

ssize_t
write(int fildes, const void* data, uint32_t dataLen)
{
    return write(fildes, {{data, dataLen}});
}

ssize_t
write(int fildes,
       std::initializer_list<std::pair<const void*, uint32_t>> data)
{
    size_t totalBytes = 0;
    uint32_t iovcnt = DLog::downCast<uint32_t>(data.size());
    struct iovec iov[iovcnt];
    uint32_t i = 0;
    for (auto it = data.begin(); it != data.end(); ++it) {
        iov[i].iov_base = const_cast<void*>(it->first);
        iov[i].iov_len = it->second;
        totalBytes += it->second;
        ++i;
    }

    size_t bytesRemaining = totalBytes;
    while (true) {
         ssize_t written = System::writev(fildes, iov, iovcnt);
         if (written == -1) {
             if (errno == EINTR)
                 continue;
             return -1;
         }
         bytesRemaining -= written;
         if (bytesRemaining == 0)
             return totalBytes;
         for (uint32_t i = 0; i < iovcnt; ++i) {
             if (iov[i].iov_len < static_cast<size_t>(written)) {
                 written -= iov[i].iov_len;
                 iov[i].iov_len = 0;
             } else if (iov[i].iov_len >= static_cast<size_t>(written)) {
                 iov[i].iov_len -= written;
                 iov[i].iov_base = (static_cast<char*>(iov[i].iov_base) +
                                    written);
                 break;
             }
         }
    }
}

// class FileContents

FileContents::FileContents(const std::string& path)
    : path(path)
    , fd(-1)
    , fileLen(0)
    , map(NULL)
{
    fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        PANIC("Could not open %s: %s",
              path.c_str(), strerror(errno));
    }
    struct stat stat;
    if (fstat(fd, &stat) != 0) {
        PANIC("Could not stat %s: %s",
              path.c_str(), strerror(errno));
    }
    if (stat.st_size > ~0U) {
        PANIC("File %s too big",
              path.c_str());
    }
    fileLen = DLog::downCast<uint32_t>(stat.st_size);

    map = mmap(NULL, fileLen, PROT_READ, MAP_SHARED, fd, 0);
    if (map == NULL) {
        PANIC("Could not map %s: %s",
              path.c_str(), strerror(errno));
    }
}

FileContents::~FileContents()
{
    if (munmap(const_cast<void*>(map), fileLen) != 0) {
        WARN("Failed to munmap file %s: %s",
             path.c_str(), strerror(errno));
    }
    if (close(fd) != 0) {
        WARN("Failed to close file %s: %s",
             path.c_str(), strerror(errno));
    }
}

void
FileContents::copy(uint32_t offset, void* buf, uint32_t length)
{
    if (copyPartial(offset, buf, length) != length) {
        PANIC("File %s too short or corrupt",
              path.c_str());
    }
}

uint32_t
FileContents::copyPartial(uint32_t offset, void* buf, uint32_t maxLength)
{
    if (offset >= fileLen)
        return 0;
    uint32_t length = std::min(fileLen - offset, maxLength);
    memcpy(buf, static_cast<const char*>(map) + offset, length);
    return length;
}

const void*
FileContents::getHelper(uint32_t offset, uint32_t length)
{
    if (length != 0 && offset + length > fileLen) {
        PANIC("File %s too short or corrupt",
              path.c_str());
    }
    return static_cast<const char*>(map) + offset;
}

} // namespace LogCabin::Storage::FilesystemUtil
} // namespace LogCabin::Storage
} // namespace LogCabin
