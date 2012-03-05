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

#include "include/Debug.h"
#include "Core/Config.h"
#include "Storage/Factory.h"
#include "Storage/FilesystemModule.h"
#include "Storage/MemoryModule.h"

namespace LogCabin {
namespace Storage {
namespace Factory {

std::unique_ptr<Module>
createStorageModule(const Core::Config& config)
{
    std::unique_ptr<Module> module;
    std::string moduleName = config.read<std::string>("storageModule");
    LOG(NOTICE, "Using '%s' storage module", moduleName.c_str());
    if (moduleName == "memory") {
        module.reset(new MemoryModule());
    } else if (moduleName == "filesystem") {
        module.reset(new FilesystemModule(config));
    } else {
        PANIC("Bad storage module given: %s\n"
              "Choices are: memory, filesystem",
              moduleName.c_str());
    }
    return module;
}

} // namespace LogCabin::Storage::Factory
} // namespace LogCabin::Storage
} // namespace LogCabin
