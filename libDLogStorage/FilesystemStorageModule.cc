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

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#include <algorithm>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "../build/libDLogStorage/DumbFilesystem.pb.h"
#include "Debug.h" // TODO(ongaro): Move Debug to common
#include "FilesystemStorageModule.h"
#include "FilesystemUtil.h"
#include "WorkDispatcher.h"

namespace DLog {

namespace Storage {

// class FilesystemStorageModule

FilesystemStorageModule::FilesystemStorageModule(const std::string& path)
    : path(path)
{
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            PANIC("Failed to create directory for FilesystemStorageModule:"
                  " mkdir(%s) failed: %s", path.c_str(), strerror(errno));
        }
    }
}

std::vector<LogId>
FilesystemStorageModule::getLogs()
{
    std::vector<std::string> filenames = FilesystemUtil::ls(path);
    std::vector<LogId> ret;
    for (auto it = filenames.begin(); it != filenames.end(); ++it) {
        const std::string& filename = *it;
        LogId logId;
        unsigned bytesConsumed;
        int matched = sscanf(filename.c_str(), "%016lx%n", // NOLINT
                             &logId, &bytesConsumed);
        if (matched != 1 || bytesConsumed != filename.length()) {
            WARN("%s doesn't look like a valid log ID (from %s)",
                 filename.c_str(),
                 (path + "/" + filename).c_str());
            continue;
        }
        ret.push_back(logId);
    }
    return ret;
}

namespace {

/// Used by openLog.
class WorkerOpenLogCompletion : public WorkDispatcher::CompletionCallback  {
    typedef StorageModule::OpenCallback OpenCallback;
    WorkerOpenLogCompletion(Ref<OpenCallback> openCompletion,
                            Ref<Log> log)
        : openCompletion(openCompletion)
        , log(log)
    {
    }
  public:
    void completed() {
        openCompletion->opened(log);
    }
    Ref<OpenCallback> openCompletion;
    Ref<Log> log;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<WorkerOpenLogCompletion>;
};

/// Used by openLog.
class WorkerOpenLog : public WorkDispatcher::WorkerCallback  {
    typedef StorageModule::OpenCallback OpenCallback;
    WorkerOpenLog(LogId logId,
                  const std::string& path,
                  Ref<OpenCallback> openCompletion)
        : logId(logId)
        , path(path)
        , openCompletion(openCompletion)
    {
    }
  public:
    void run() {
        Ref<Log> newLog = make<FilesystemLog>(logId, path);
        auto completion = make<WorkerOpenLogCompletion>(openCompletion,
                                                        newLog);
        workDispatcher->scheduleCompletion(completion);
    }
    const LogId logId;
    const std::string path;
    Ref<OpenCallback> openCompletion;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<WorkerOpenLog>;
};

} // anonymous namespace

void
FilesystemStorageModule::openLog(LogId logId,
                                 Ref<OpenCallback> openCompletion)
{
    auto work = make<WorkerOpenLog>(logId, getLogPath(logId),
                                    openCompletion);
    workDispatcher->scheduleWork(work);
}

namespace {

/// Used by deleteLog.
class WorkerDeleteLogCompletion : public WorkDispatcher::CompletionCallback  {
    typedef StorageModule::DeleteCallback DeleteCallback;
    WorkerDeleteLogCompletion(LogId logId,
                              Ref<DeleteCallback> deleteCompletion)
        : logId(logId)
        , deleteCompletion(deleteCompletion)
    {
    }
  public:
    void completed() {
        deleteCompletion->deleted(logId);
    }
    LogId logId;
    Ref<DeleteCallback> deleteCompletion;

    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<WorkerDeleteLogCompletion>;
};

/// Used by deleteLog.
class WorkerDeleteLog : public WorkDispatcher::WorkerCallback  {
    WorkerDeleteLog(const std::string& path,
                    Ref<WorkerDeleteLogCompletion> completion)
        : path(path)
        , completion(completion)
    {
    }
  public:
    void run() {
        FilesystemUtil::remove(path);
        workDispatcher->scheduleCompletion(completion);
    }
    const std::string path;
    Ref<WorkerDeleteLogCompletion> completion;

    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<WorkerDeleteLog>;
};
} // anonymous namespace

void
FilesystemStorageModule::deleteLog(LogId logId,
                                   Ref<DeleteCallback> deleteCompletion)
{
    auto completion = make<WorkerDeleteLogCompletion>(logId,
                                                      deleteCompletion);
    auto work = make<WorkerDeleteLog>(getLogPath(logId),
                                      completion);
    workDispatcher->scheduleWork(work);
}

std::string
FilesystemStorageModule::getLogPath(LogId logId) const
{
    return format("%s/%016lx", path.c_str(), logId);
}

// class FilesystemLog

FilesystemLog::FilesystemLog(LogId logId, const std::string& path)
    : Log(logId)
    , path(path)
    , headId(NO_ENTRY_ID)
    , entries()
    , writing(false)
    , writeQueue()
{
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            PANIC("Failed to create directory for FilesystemLog:"
                  " mkdir(%s) failed: %s", path.c_str(), strerror(errno));
        }
    }

    std::vector<EntryId> entryIds = getEntryIds();
    std::sort(entryIds.begin(), entryIds.end());
    for (auto it = entryIds.begin(); it != entryIds.end(); ++it)
        read(*it);
}

std::deque<LogEntry>
FilesystemLog::readFrom(EntryId start)
{
    std::deque<LogEntry> ret;
    for (auto it = entries.rbegin();
         it != entries.rend();
         ++it) {
        LogEntry& entry = *it;
        if (entry.entryId < start)
            break;
        ret.push_front(entry);
    }
    return ret;
}

void
FilesystemLog::append(LogEntry entry,
                      Ref<AppendCallback> appendCompletion)
{
    writeQueue.push_back({entry, appendCompletion});
    scheduleAppends();
}

/// Used by scheduleAppends.
class FilesystemLog::WorkerAppendCompletion
                        : public WorkDispatcher::CompletionCallback  {
    WorkerAppendCompletion(Ref<FilesystemLog> log,
                           WriteQueue&& writeQueue)
        : log(log)
        , writeQueue(std::move(writeQueue))
    {
    }
  public:
    void completed() {
        // Update in-memory data structures.
        for (auto it = writeQueue.begin(); it != writeQueue.end(); ++it) {
            const LogEntry& entry = it->first;
            Ref<AppendCallback> appendCompletion = it->second;
            log->headId = entry.entryId;
            log->entries.push_back(entry);
        }

        // Schedule more writes if they've already queued up.
        log->writing = false;
        log->scheduleAppends();

        // Call append completions. This is done in a separate loop so that it
        // can be concurrent with storage writes.
        for (auto it = writeQueue.begin(); it != writeQueue.end(); ++it) {
            const LogEntry& entry = it->first;
            Ref<AppendCallback> appendCompletion = it->second;
            appendCompletion->appended(entry);
        }
    }
    Ref<FilesystemLog> log;
    const WriteQueue writeQueue;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<WorkerAppendCompletion>;
};

/// Used by scheduleAppends.
class FilesystemLog::WorkerAppend : public WorkDispatcher::WorkerCallback  {
    WorkerAppend(Ref<FilesystemLog> log,
                 WriteQueue&& writeQueue)
        : log(log)
        , headId(log->headId)
        , writeQueue(std::move(writeQueue))
    {
        LOG(DBG, "supposed to write %lu entries", this->writeQueue.size());
    }
  public:
    // Write to disk.
    void run() {
        // Be careful not to modify 'log' from worker threads.
        for (auto it = writeQueue.begin(); it != writeQueue.end(); ++it) {
            LogEntry& entry = it->first;
            if (headId == NO_ENTRY_ID)
                headId = 0;
            else
                ++headId;
            entry.logId = log->getLogId();
            entry.entryId = headId;
            LOG(DBG, "writing (%lu, %lu)", entry.logId, entry.entryId);
            log->write(entry);
        }
        auto completion = make<WorkerAppendCompletion>(log,
                                                       std::move(writeQueue));
        workDispatcher->scheduleCompletion(completion);
    }
    Ref<FilesystemLog> log;
    EntryId headId;
    WriteQueue writeQueue;
    friend class DLog::MakeHelper;
    friend class DLog::RefHelper<WorkerAppend>;
};

void
FilesystemLog::scheduleAppends()
{
    if (!writing && !writeQueue.empty()) {
        writing = true;
        WriteQueue localWriteQueue;
        writeQueue.swap(localWriteQueue);
        auto work = make<WorkerAppend>(Ref<FilesystemLog>(*this),
                                       std::move(localWriteQueue));
        workDispatcher->scheduleWork(work);
    }
}

std::vector<EntryId>
FilesystemLog::getEntryIds()
{
    std::vector<std::string> filenames = FilesystemUtil::ls(path);
    std::vector<EntryId> entryIds;
    for (auto it = filenames.begin(); it != filenames.end(); ++it) {
        const std::string& filename = *it;
        EntryId entryId;
        unsigned bytesConsumed;
        int matched = sscanf(filename.c_str(), "%016lx%n", // NOLINT
                             &entryId, &bytesConsumed);
        if (matched != 1 || bytesConsumed != filename.length()) {
            WARN("%s doesn't look like a valid entry ID (from %s)",
                 filename.c_str(),
                 (path + "/" + filename).c_str());
            continue;
        }
        entryIds.push_back(entryId);
    }
    return entryIds;
}

std::string
FilesystemLog::getEntryPath(EntryId entryId) const
{
    return format("%s/%016lx", path.c_str(), entryId);
}

void
FilesystemLog::read(EntryId entryId)
{
    const std::string entryPath = getEntryPath(entryId);
    int fd = open(entryPath.c_str(), O_RDONLY);
    if (fd == -1)
        PANIC("Could not open %s: %s", entryPath.c_str(), strerror(errno));
    ::google::protobuf::io::FileInputStream inputStream(fd);
    inputStream.SetCloseOnDelete(true);
    ProtoBuf::DumbFilesystem::LogEntry contents;
    if (!contents.ParseFromZeroCopyStream(&inputStream)) {
        PANIC("Failed to parse log entry from %s. "
              "It is missing the following required fields: %s",
              entryPath.c_str(),
              contents.InitializationErrorString().c_str());
    }
    Ref<Chunk> data = NO_DATA;
    if (contents.has_data()) {
        const std::string& contentsData = contents.data();
        data = Chunk::makeChunk(contentsData.c_str(),
                                downCast<uint32_t>(contentsData.length()));
    }
    const auto& contentsInv = contents.invalidations();
    std::vector<EntryId> invalidations(contentsInv.begin(), contentsInv.end());
    LogEntry entry(getLogId(),
                   entryId,
                   contents.create_time(),
                   data,
                   invalidations);
    entries.push_back(entry);
    if (headId == NO_ENTRY_ID || headId < entryId)
        headId = entryId;
}

void
FilesystemLog::write(const LogEntry& entry)
{
    ProtoBuf::DumbFilesystem::LogEntry contents;
    contents.set_create_time(entry.createTime);
    if (entry.data != NO_DATA) {
        contents.set_data(entry.data->getData(),
                          entry.data->getLength());
    }
    for (auto it = entry.invalidations.begin();
         it != entry.invalidations.end();
         ++it) {
        contents.add_invalidations(*it);
    }

    const std::string entryPath = getEntryPath(entry.entryId);
    int fd = open(entryPath.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0644);
    if (fd == -1)
        PANIC("Could not create %s: %s", entryPath.c_str(), strerror(errno));
    ::google::protobuf::io::FileOutputStream outputStream(fd);
    outputStream.SetCloseOnDelete(true);
    if (!contents.SerializeToZeroCopyStream(&outputStream))
        PANIC("Failed to serialize log entry");
}


} // namespace DLog::Storage
} // namespace DLog
