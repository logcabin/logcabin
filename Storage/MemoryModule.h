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
 * Contains MemoryStorageModule.
 */

#include <unordered_map>

#include "Storage/Log.h"
#include "Storage/Module.h"

#ifndef LOGCABIN_STORAGE_MEMORYMODULE_H
#define LOGCABIN_STORAGE_MEMORYMODULE_H

namespace LogCabin {
namespace Storage {

/**
 * An in-memory Log. See MemoryModule.
 */
class MemoryLog : public Log {
  public:
    explicit MemoryLog(LogId logId);
    ~MemoryLog();
    EntryId getLastId() const { return headId; }
    std::deque<const LogEntry*> readFrom(EntryId start) const;
    EntryId append(LogEntry entry);
  private:
    EntryId headId;
    std::deque<LogEntry> entries;
};

/**
 * A storage module that does not provide durability.
 * This is a very simple storage module that is useful for testing. Everything
 * stored here is kept in volatile RAM only and is gone when this storage
 * module is destroyed.
 */
class MemoryModule : public Module {
  public:
    MemoryModule();
    ~MemoryModule();
    std::vector<LogId> getLogs();
    MemoryLog* openLog(LogId logId);
    void deleteLog(LogId logId);

  private:
    /**
     * This operation is unique to MemoryModule and is used for testing. It
     * allows you to give a log back to the MemoryModule so that this same log
     * is returned on the next call to openLog() with the same ID.
     * \param log
     *      A log previously returned by openLog().
     */
    void putLog(MemoryLog* log);

    /**
     * This map exists to support the putLog() operation.
     * It contains all such logs that have been stashed away by putLog().
     * This map owns the MemoryLog: it should be deleted when its containing
     * map entry is deleted. (The value is not a unique_ptr<MemoryLog> because
     * libstdc++ 4.4 doesn't support placing those in maps.)
     */
    std::unordered_map<LogId, MemoryLog*> logs;
};

} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_MEMORYMODULE_H */
