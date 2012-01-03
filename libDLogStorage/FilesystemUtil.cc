/* Copyright (c) 2011 Stanford University
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

#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "FilesystemUtil.h"
#include "Debug.h"

namespace DLog {
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

std::string
tmpnam()
{
    char d[L_tmpnam];
    std::string path = ::tmpnam(d);
    assert(path != "" && path != "/");
    return path;
}

} // namespace DLog::Storage::FilesystemUtil
} // namespace DLog::Storage
} // namespace DLog
