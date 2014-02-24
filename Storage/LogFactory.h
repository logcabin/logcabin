/* Copyright (c) 2014 Stanford University
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

#include <memory>

#include "Core/Config.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/Log.h"

#ifndef LOGCABIN_STORAGE_LOGFACTORY_H
#define LOGCABIN_STORAGE_LOGFACTORY_H

namespace LogCabin {
namespace Storage {
namespace LogFactory {

/**
 * Construct and return a Log object.
 * \param config
 *      Determines which concrete type of Log to construct.
 *      PANICs if this is invalid.
 * \param parentDir
 *      Log implementations that write to the filesystem should place their
 *      files within this directory.
 * \return
 *      The newly constructed Log instance.
 */
std::unique_ptr<Log>
makeLog(const Core::Config& config,
        const FilesystemUtil::File& parentDir);

} // namespace LogCabin::Storage::LogFactory
} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_LOGFACTORY_H */
