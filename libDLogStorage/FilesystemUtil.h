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

/**
 * \file
 * Contains utilities for working with the filesystem.
 */

#include <string>
#include <vector>

#include "Common.h"

#ifndef LIBDLOGSTORAGE_FILESYSTEMUTIL_H
#define LIBDLOGSTORAGE_FILESYSTEMUTIL_H

namespace DLog {
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
 * Return a path suitable for use as a temporary file or directory that is
 * likely to not exist.
 * \warning
 *      Be aware of the race condition that other processes may take this name
 *      between this call and the caller creating the file or directory.
 */
std::string tmpnam();

} // namespace DLog::Storage::FilesystemUtil
} // namespace DLog::Storage
} // namespace DLog

#endif /* LIBDLOGSTORAGE_FILESYSTEMUTIL_H */
