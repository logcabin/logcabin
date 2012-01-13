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

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>

#include "Checksum.h"
#include "Config.h"
#include "Debug.h"
#include "FilesystemStorageModule.h"
#include "FilesystemUtil.h"
#include "WorkDispatcher.h"

namespace DLog {

namespace Storage {

// class FilesystemStorageModule

FilesystemStorageModule::FilesystemStorageModule(const Config& config)
    : path(config.read<std::string>("storagePath"))
    , checksumAlgorithm(config.read<std::string>("checksum", "SHA-1"))
{
    { // Ensure the checksum algorithm is valid.
        char buf[Checksum::MAX_LENGTH];
        Checksum::calculate(checksumAlgorithm.c_str(), "", 0, buf);
    }

    LOG(NOTICE, "Using filesystem storage module at %s", path.c_str());
    if (mkdir(path.c_str(), 0755) == 0) {
        FilesystemUtil::syncDir(path + "/..");
    } else {
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
                  const std::string& checksumAlgorithm,
                  Ref<OpenCallback> openCompletion)
        : logId(logId)
        , path(path)
        , checksumAlgorithm(checksumAlgorithm)
        , openCompletion(openCompletion)
    {
    }
  public:
    void run() {
        Ref<Log> newLog = make<FilesystemLog>(logId, path,
                                              checksumAlgorithm);
        auto completion = make<WorkerOpenLogCompletion>(openCompletion,
                                                        newLog);
        workDispatcher->scheduleCompletion(completion);
    }
    const LogId logId;
    const std::string path;
    const std::string checksumAlgorithm;
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
                                    checksumAlgorithm,
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

FilesystemLog::FilesystemLog(LogId logId,
                             const std::string& path,
                             const std::string& checksumAlgorithm)
    : Log(logId)
    , path(path)
    , checksumAlgorithm(checksumAlgorithm)
    , headId(NO_ENTRY_ID)
    , entries()
    , writing(false)
    , writeQueue()
{
    if (mkdir(path.c_str(), 0755) == 0) {
        FilesystemUtil::syncDir(path + "/..");
    } else {
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

namespace {
namespace Header {

/**
 * This is a small header that immediately follows the checksum.
 * The format of this header shouldn't change over time.
 */
struct Fixed {
    /**
     * Convert the contents to host order from big endian (how this header
     * should be stored on disk).
     */
    void fromBigEndian() {
        checksumCoverage = be32toh(checksumCoverage);
        version = be32toh(version);
    }
    /**
     * Convert the contents to big endian (how this header should be
     * stored on disk) from host order.
     */
    void toBigEndian() {
        checksumCoverage = htobe32(checksumCoverage);
        version = htobe32(version);
    }
    /**
     * The number of bytes including and following this header that the
     * checksum preceding this header should cover.
     */
    uint32_t checksumCoverage;
    /**
     * The version of the header which follows this one.
     * This should be 0 for now.
     */
    uint32_t version;
} __attribute__((packed));

/**
 * This header follows the Fixed header.
 */
struct Version0 {
    /**
     * Convert the contents to host order from big endian (how this header
     * should be stored on disk).
     */
    void fromBigEndian() {
        logId = be64toh(logId);
        entryId = be64toh(entryId);
        createTime = be64toh(createTime);
        invalidationsLen = be32toh(invalidationsLen);
        dataChecksumLen = be32toh(dataChecksumLen);
        dataLen = be32toh(dataLen);
    }
    /**
     * Convert the contents to big endian (how this header should be
     * stored on disk) from host order.
     */
    void toBigEndian() {
        logId = htobe64(logId);
        entryId = htobe64(entryId);
        createTime = htobe64(createTime);
        invalidationsLen = htobe32(invalidationsLen);
        dataChecksumLen = htobe32(dataChecksumLen);
        dataLen = htobe32(dataLen);
    }
    /// See LogEntry.
    uint64_t logId;
    /// See LogEntry.
    uint64_t entryId;
    /// See LogEntry.
    uint64_t createTime;
    /// The number of entry invalidations this entry contains.
    uint32_t invalidationsLen;
    /// The number of bytes in the data checksum, including the null
    /// terminator. This should be 0 if the user supplied no data.
    uint32_t dataChecksumLen;
    /// The number of bytes in the data.
    uint32_t dataLen;
} __attribute__((packed));

} // DLog::Storage::<anonymous>::Header
} // DLog::Storage::<anonymous>

void
FilesystemLog::read(EntryId entryId)
{
    const std::string entryPath = getEntryPath(entryId);
    FilesystemUtil::FileContents file(entryPath);
    uint32_t fileOffset = 0;

    // Copy out the checksum.
    char checksum[Checksum::MAX_LENGTH];
    uint32_t bytesRead = file.copyPartial(fileOffset,
                                          checksum, sizeof32(checksum));
    uint32_t checksumBytes = Checksum::length(checksum, bytesRead);
    if (checksumBytes == 0)
        PANIC("File %s corrupt", entryPath.c_str());
    fileOffset += checksumBytes;

    // Copy out the fixed header, which contains how many bytes the checksum
    // covers.
    Header::Fixed fixedHeader;
    file.copy(fileOffset, &fixedHeader, sizeof32(fixedHeader));
    fixedHeader.fromBigEndian();

    // Verify the checksum.
    const void* checksumArea = file.get(fileOffset,
                                        fixedHeader.checksumCoverage);
    fileOffset += sizeof32(fixedHeader);
    std::string error = Checksum::verify(checksum,
                                         checksumArea,
                                         fixedHeader.checksumCoverage);
    if (!error.empty()) {
        PANIC("Checksum verification failure on %s: %s",
              entryPath.c_str(), error.c_str());
    }

    // Copy out the regular header.
    if (fixedHeader.version > 0) {
        PANIC("The running code is too old to understand the version of the "
              "file format encountered in %s (version %u)",
              entryPath.c_str(), fixedHeader.version);
    }
    Header::Version0 header;
    file.copy(fileOffset, &header, sizeof32(header));
    header.fromBigEndian();
    fileOffset += sizeof32(header);

    // Run basic sanity checks on the regular header.
    // TODO(ongaro): The uuid should be included in the checksum.
    if (header.logId != getLogId()) {
        PANIC("Expected log ID %lu, found %lu in %s",
              getLogId(), header.logId, entryPath.c_str());
    }
    if (header.entryId != entryId) {
        PANIC("Expected entry ID %lu, found %lu in %s",
              entryId, header.entryId, entryPath.c_str());
    }
    const uint32_t invalidationsBytes = (header.invalidationsLen *
                                         sizeof32(EntryId));
    if (fixedHeader.checksumCoverage != (sizeof(fixedHeader) +
                                         sizeof(header) +
                                         invalidationsBytes +
                                         header.dataChecksumLen)) {
        PANIC("File %s corrupt", entryPath.c_str());
    }

    // Copy out the invalidations array.
    const EntryId* invalidationsArray =
        file.get<EntryId>(fileOffset, invalidationsBytes);
    fileOffset += invalidationsBytes;
    std::vector<EntryId> invalidations(
                invalidationsArray,
                invalidationsArray + header.invalidationsLen);

    // Copy out the data.
    Ref<Chunk> data = NO_DATA;
    if (header.dataChecksumLen > 0) {
        // Get and verify the data checksum.
        const char* dataChecksum =
            file.get<char>(fileOffset, header.dataChecksumLen);
        fileOffset += header.dataChecksumLen;
        if (Checksum::length(dataChecksum, header.dataChecksumLen) !=
            header.dataChecksumLen) {
            PANIC("File %s corrupt", entryPath.c_str());
        }
        const void* rawData = file.get(fileOffset, header.dataLen);
        fileOffset += header.dataLen;
        error = Checksum::verify(dataChecksum, rawData, header.dataLen);
        if (!error.empty()) {
            PANIC("Checksum verification failure on %s: %s",
                  entryPath.c_str(), error.c_str());
        }
        // Create the data chunk.
        data = Chunk::makeChunk(rawData, header.dataLen);
    }

    // Create the LogEntry and add it to this Log.
    LogEntry entry(getLogId(),
                   entryId,
                   header.createTime,
                   data,
                   invalidations);
    entries.push_back(entry);
    if (headId == NO_ENTRY_ID || headId < entryId)
        headId = entryId;
}

void
FilesystemLog::write(const LogEntry& entry)
{
    // Calculate the data checksum.
    char dataChecksum[Checksum::MAX_LENGTH];
    uint32_t dataChecksumLen = 0;
    if (entry.data != NO_DATA) {
        dataChecksumLen = Checksum::calculate(checksumAlgorithm.c_str(),
                                              entry.data->getData(),
                                              entry.data->getLength(),
                                              dataChecksum);
    }

    // Calculate useful lengths.
    const uint32_t invalidationsBytes =
            downCast<uint32_t>(entry.invalidations.size()) * sizeof32(EntryId);
    const uint32_t checksumCoverage = sizeof32(Header::Fixed) +
                                      sizeof32(Header::Version0) +
                                      invalidationsBytes +
                                      dataChecksumLen;

    // Fill in the fixed header.
    Header::Fixed fixedHeader;
    fixedHeader.checksumCoverage = checksumCoverage;
    fixedHeader.version = 0;
    fixedHeader.toBigEndian();

    // Fill in the header.
    Header::Version0 header;
    header.logId = getLogId();
    header.entryId = entry.entryId;
    header.createTime = entry.createTime;
    header.invalidationsLen = downCast<uint32_t>(entry.invalidations.size());
    header.dataChecksumLen = dataChecksumLen;
    header.dataLen = entry.data->getLength();
    header.toBigEndian();

    // Calculate the checksum.
    char checksum[Checksum::MAX_LENGTH];
    uint32_t checksumLen = Checksum::calculate(
                checksumAlgorithm.c_str(),
                {{ &fixedHeader, sizeof32(fixedHeader) },
                 { &header, sizeof32(header) },
                 { entry.invalidations.data(), invalidationsBytes },
                 { dataChecksum, dataChecksumLen }},
                checksum);

    // Open the file, write to it, and close it.
    const std::string entryPath = getEntryPath(entry.entryId);
    int fd = open(entryPath.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0644);
    if (fd == -1)
        PANIC("Could not create %s: %s", entryPath.c_str(), strerror(errno));
    if (FilesystemUtil::write(fd, {
            { checksum, checksumLen},
            { &fixedHeader, sizeof32(fixedHeader) },
            { &header, sizeof32(header) },
            { entry.invalidations.data(), invalidationsBytes },
            { dataChecksum, dataChecksumLen },
            { entry.data->getData(), entry.data->getLength() },
        }) == -1) {
        PANIC("Filesystem write to %s failed: %s",
              entryPath.c_str(), strerror(errno));
    }
    if (fsync(fd) != 0) {
        PANIC("Could not fsync %s: %s",
              entryPath.c_str(), strerror(errno));
    }
    if (close(fd) != 0) {
        PANIC("Failed to close log entry file %s: %s",
              entryPath.c_str(), strerror(errno));
    }
}


} // namespace DLog::Storage
} // namespace DLog
