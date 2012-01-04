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
 * Contains DumbFilesystemStorageModule.
 */

#include <list>
#include <unordered_map>

#include "DLogStorage.h"

#ifndef LIBDLOGSTORAGE_DUMBFILESYSTEMSTORAGEMODULE_H
#define LIBDLOGSTORAGE_DUMBFILESYSTEMSTORAGEMODULE_H

namespace DLog {
namespace Storage {

/**
 * This is a naive implementation of a storage module that uses the filesystem.
 * It has a number of limitations:
 * - Creates one file per log entry
 * - Never cleans up used storage space
 * - Does not detect data corruption
 * - Does not detect metadata corruption
 * - Does all file I/O in the main thread
 * - Does not call sync or fsync
 * Therefore, it is not recommended for actual use.
 */
class DumbFilesystemStorageModule : public StorageModule {
  private:
    /**
     * Constructor.
     * \param path
     *      A filesystem path for this storage module to operate in.
     *      The parent directory of 'path' must already exist.
     */
    explicit DumbFilesystemStorageModule(const std::string& path);
  public:
    std::vector<LogId> getLogs();
    void openLog(LogId logId, Ref<OpenCallback> openCompletion);
    void deleteLog(LogId logId, Ref<DeleteCallback> deleteCompletion);
  private:
    /// Return the filesystem path for a particular log.
    std::string getLogPath(LogId logId) const;
    /// See constructor.
    const std::string path;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<DumbFilesystemStorageModule>;
};

/**
 * A Log for DumbFilesystemStorageModule.
 */
class DumbFilesystemLog : public Log {
  private:
    DumbFilesystemLog(LogId logId, const std::string& path);
  public:
    EntryId getLastId() { return headId; }
    std::deque<LogEntry> readFrom(EntryId start);
    void append(LogEntry entry, Ref<AppendCallback> appendCompletion);
  private:
    std::vector<EntryId> getEntryIds();
    /// Return the filesystem path for a particular entry.
    std::string getEntryPath(EntryId entryId) const;
    /// Read an entry from the filesystem into memory.
    void read(EntryId entryId);
    /// Read an entry out to the filesystem.
    void write(const LogEntry& entry);
    /// See constructor.
    const std::string path;
    /// See getLastId().
    EntryId headId;
    /// All log entries in order.
    std::list<LogEntry> entries;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<DumbFilesystemLog>;
};


} // namespace DLog::Storage
} // namespace DLog

#endif /* LIBDLOGSTORAGE_DUMBFILESYSTEMSTORAGEMODULE_H */
