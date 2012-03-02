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

/**
 * \file
 * Some basic types used in LogCabin::Storage.
 */

#include <cinttypes>

#ifndef LOGCABIN_STORAGE_TYPES_H
#define LOGCABIN_STORAGE_TYPES_H

namespace LogCabin {
namespace Storage {

/**
 * A unique ID for the log.
 * The ID space is assigned by Server::LogManager. These are never reused.
 * The special value 'NO_LOG_ID' is usually used to mean no such log exists.
 */
typedef uint64_t LogId;

/**
 * A special value for LogId that usually indicates no such log exists.
 */
static const LogId NO_LOG_ID = ~0UL;

/**
 * A unique ID for a log entry.
 * These start at 0 and increment. They are never reused.
 * The special value 'NO_ENTRY_ID' is usually used to mean no such entry
 * exists.
 */
typedef uint64_t EntryId;

/**
 * A special value for EntryId that usually indicates no such entry exists.
 */
static const EntryId NO_ENTRY_ID = ~0UL;

/**
 * TODO(ongaro): Document this.
 */
typedef uint64_t TimeStamp;

} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_TYPES_H */
