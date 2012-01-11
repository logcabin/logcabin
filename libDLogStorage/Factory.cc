/* Copyright (c) 2012 Stanford University
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

#include <string>

#include "Config.h"
#include "Debug.h"
#include "DLogStorage.h"

#include "DumbFilesystemStorageModule.h"
#include "FilesystemStorageModule.h"
#include "MemoryStorageModule.h"

namespace DLog {
namespace Storage {
namespace Factory {

Ref<StorageModule>
createStorageModule(const Config& config)
{
    std::string moduleName = config.read<std::string>("storageModule");
    if (moduleName == "dumbFilesystem") {
        std::string path = config.read<std::string>("storagePath");
        LOG(NOTICE, "Using dumbFilesystem storage module at %s",
                    path.c_str());
        return make<DumbFilesystemStorageModule>(path);
    } else if (moduleName == "filesystem") {
        std::string path = config.read<std::string>("storagePath");
        LOG(NOTICE, "Using filesystem storage module at %s",
                    path.c_str());
        return make<FilesystemStorageModule>(path);
    } else if (moduleName == "memory") {
        LOG(NOTICE, "Using memory storage module");
        return make<MemoryStorageModule>();
    } else {
        PANIC("Bad storage module given: %s\n"
              "Choices are: filesystem, dumbFilesystem, memory",
              moduleName.c_str());
    }
}

} // namespace DLog::Factory
} // namespace DLog::Storage
} // namespace DLog
