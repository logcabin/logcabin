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
 * Contains MemoryStorageModule.
 */

#include <list>

#include "DLogStorage.h"

#ifndef LIBDLOGSTORAGE_MEMORYSTORAGEMODULE_H
#define LIBDLOGSTORAGE_MEMORYSTORAGEMODULE_H

namespace DLog {
namespace Storage {

/**
 * An in-memory Log. See MemoryStorageModule.
 */
class MemoryLog : public Log {
  private:
    explicit MemoryLog(LogId logId);
  public:
    EntryId getLastId() { return headId; }
    std::deque<LogEntry> readFrom(EntryId start);
    void append(LogEntry& entry,
                std::unique_ptr<AppendCallback>&& appendCompletion);

  private:
    EntryId headId;
    std::list<LogEntry> entries;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<MemoryLog>;
};

/**
 * A storage module that does not provide durability.
 * This is a very simple storage module that is useful for testing. Everything
 * stored here is kept in volatile RAM only and is gone when this storage
 * module is destroyed.
 */
class MemoryStorageModule : public StorageModule {
  public:
    MemoryStorageModule();
    std::vector<Ref<Log>> getLogs();
    Ref<Log> createLog(LogId logId);
    void deleteLog(LogId logId,
                   std::unique_ptr<DeleteCallback>&& deleteCompletion);
    std::unordered_map<LogId, Ref<Log>> logs;
};

} // namespace DLog::Storage
} // namespace DLog

#endif /* DLOGSTORAGE_H */
