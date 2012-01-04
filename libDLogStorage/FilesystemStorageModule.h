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
 * Contains FilesystemStorageModule.
 */

#include <list>
#include <deque>
#include <unordered_map>
#include <vector>

#include "DLogStorage.h"

#ifndef LIBDLOGSTORAGE_FILESYSTEMSTORAGEMODULE_H
#define LIBDLOGSTORAGE_FILESYSTEMSTORAGEMODULE_H

namespace DLog {
namespace Storage {

/**
 * This will become the default storage module that uses the filesystem.
 * It has a number of limitations for now:
 * - Creates one file per log entry
 * - Never cleans up used storage space
 * - Does not detect data corruption
 * - Does not detect metadata corruption
 * - Does not call sync or fsync
 * Therefore, it is not yet recommended for actual use.
 */
class FilesystemStorageModule : public StorageModule {
  private:
    /**
     * Constructor.
     * \param path
     *      A filesystem path for this storage module to operate in.
     *      The parent directory of 'path' must already exist.
     */
    explicit FilesystemStorageModule(const std::string& path);
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
    friend class DLog::RefHelper<FilesystemStorageModule>;
};

/**
 * A Log for FilesystemStorageModule.
 */
class FilesystemLog : public Log {
  private:
    FilesystemLog(LogId logId, const std::string& path);
  public:
    EntryId getLastId() { return headId; }
    std::deque<LogEntry> readFrom(EntryId start);
    void append(LogEntry entry, Ref<AppendCallback> appendCompletion);
  private:
    /**
     * If no work request is outstanding, empty writeQueue into a new work
     * request. See also 'writing'.
     */
    void scheduleAppends();
    /**
     * Scan the entries on the filesystem and return their IDs.
     * \return
     *      The ID of each entry, in no specified order.
     */
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
    /**
     * This is true while a write work request is outstanding.
     * It is used to ensure that only one work request is outstanding at a
     * time. This in turn guarantees that storage writes are fully ordered for
     * this log.
     */
    bool writing;
    /// These make up a WriteQueue.
    typedef std::pair<LogEntry, Ref<AppendCallback>> WriteQueueEntry;
    /// A queue of outstanding log append requests.
    typedef std::vector<WriteQueueEntry> WriteQueue;
    /**
     * Append requests are queued up here until they are sent to a worker for
     * writing out to the filesystem.
     */
    WriteQueue writeQueue;

    // Helper classes.
    class WorkerAppend;
    class WorkerAppendCompletion;

    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<FilesystemLog>;
};


} // namespace DLog::Storage
} // namespace DLog

#endif /* LIBDLOGSTORAGE_FILESYSTEMSTORAGEMODULE_H */
