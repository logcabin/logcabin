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

#include <deque>
#include <vector>

#include "Storage/Log.h"
#include "Storage/Module.h"

#ifndef LOGCABIN_STORAGE_FILESYSTEMMODULE_H
#define LOGCABIN_STORAGE_FILESYSTEMMODULE_H

namespace LogCabin {
namespace Storage {

/**
 * This will become the default storage module that uses the filesystem.
 * It has some limitations for now:
 * - Creates one file per log entry
 * - Never cleans up used storage space
 * Therefore, it is not yet recommended for actual use.
 */
class FilesystemModule : public Storage::Module {
  public:
    explicit FilesystemModule(const Core::Config& config);
    std::vector<LogId> getLogs();
    Log* openLog(LogId logId);
    void deleteLog(LogId logId);
  private:
    /// Return the filesystem path for a particular log.
    std::string getLogPath(LogId logId) const;
    /// See constructor.
    const std::string path;
    const std::string checksumAlgorithm;
};

/**
 * A Log for FilesystemModule.
 */
class FilesystemLog : public Storage::Log {
  private:
    FilesystemLog(LogId logId, const std::string& path,
                  const std::string& checksumAlgorithm);
  public:
    EntryId getLastId() const { return headId; }
    std::deque<const LogEntry*> readFrom(EntryId start) const;
    EntryId append(LogEntry entry);
  private:
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
    /// See constructor.
    const std::string checksumAlgorithm;
    /// See getLastId().
    EntryId headId;
    /// All log entries in order.
    std::deque<LogEntry> entries;

    // Helper classes.
    class WorkerAppend;
    class WorkerAppendCompletion;
    friend class FilesystemModule;
};


} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_FILESYSTEMMODULE_H */
