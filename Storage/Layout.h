/* Copyright (c) 2015 Diego Ongaro
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

#ifndef LOGCABIN_STORAGE_LAYOUT_H
#define LOGCABIN_STORAGE_LAYOUT_H

#include "Storage/FilesystemUtil.h"

namespace LogCabin {

// forward declaration
namespace Core {
class Config;
}

namespace Storage {

class Layout {
  public:
    Layout();
    Layout(Layout&& other);
    ~Layout();
    Layout& operator=(Layout&& other);
    void init(const Core::Config& config, uint64_t serverId);
    void init(const std::string& storagePath, uint64_t serverId);
    void initTemporary(uint64_t serverId = 1);

    FilesystemUtil::File topDir;
    FilesystemUtil::File serverDir;
    FilesystemUtil::File lockFile;
  private:
    bool removeAllFiles;
};

} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_LAYOUT_H */
