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
#include <unistd.h>

#include <algorithm>

#include "Core/Debug.h"
#include "Core/Checksum.h"
#include "Core/Config.h"
#include "Core/StringUtil.h"
#include "Core/Util.h"
#include "Storage/FilesystemModule.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Storage {

using Core::StringUtil::format;
using Core::Util::downCast;

////////// FilesystemModule //////////

FilesystemModule::FilesystemModule(const Core::Config& config)
    : path(config.read<std::string>("storagePath"))
    , checksumAlgorithm(config.read<std::string>("checksum", "SHA-1"))
{
    { // Ensure the checksum algorithm is valid.
        char buf[Core::Checksum::MAX_LENGTH];
        Core::Checksum::calculate(checksumAlgorithm.c_str(), "", 0, buf);
    }

    NOTICE("Using filesystem storage module at %s", path.c_str());
    if (mkdir(path.c_str(), 0755) == 0) {
        FilesystemUtil::syncDir(path + "/..");
    } else {
        if (errno != EEXIST) {
            PANIC("Failed to create directory for FilesystemModule:"
                  " mkdir(%s) failed: %s", path.c_str(), strerror(errno));
        }
    }
}

std::vector<LogId>
FilesystemModule::getLogs()
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
            WARNING("%s doesn't look like a valid log ID (from %s)",
                 filename.c_str(),
                 (path + "/" + filename).c_str());
            continue;
        }
        ret.push_back(logId);
    }
    return ret;
}

Log*
FilesystemModule::openLog(LogId logId)
{
    return new FilesystemLog(logId, getLogPath(logId), checksumAlgorithm);
}

void
FilesystemModule::deleteLog(LogId logId)
{
    FilesystemUtil::remove(getLogPath(logId));
}

std::string
FilesystemModule::getLogPath(LogId logId) const
{
    return format("%s/%016lx", path.c_str(), logId);
}

////////// FilesystemLog //////////

FilesystemLog::FilesystemLog(LogId logId,
                             const std::string& path,
                             const std::string& checksumAlgorithm)
    : Log(logId)
    , path(path)
    , checksumAlgorithm(checksumAlgorithm)
    , headId(NO_ENTRY_ID)
    , entries()
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

std::deque<const LogEntry*>
FilesystemLog::readFrom(EntryId start) const
{
    std::deque<const LogEntry*> ret;
    for (auto it = entries.rbegin();
         it != entries.rend();
         ++it) {
        const LogEntry& entry = *it;
        if (entry.entryId < start)
            break;
        ret.push_front(&entry);
    }
    return ret;
}

EntryId
FilesystemLog::append(LogEntry entry)
{
    if (headId == NO_ENTRY_ID)
        headId = 0;
    else
        ++headId;
    entry.logId = logId;
    entry.entryId = headId;
    VERBOSE("writing (%lu, %lu)", entry.logId, entry.entryId);
    write(entry);
    entries.push_back(std::move(entry));
    return headId;
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
            WARNING("%s doesn't look like a valid entry ID (from %s)",
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

} // LogCabin::Storage::<anonymous>::Header
} // LogCabin::Storage::<anonymous>

void
FilesystemLog::read(EntryId entryId)
{
    const std::string entryPath = getEntryPath(entryId);
    FilesystemUtil::FileContents file(entryPath);
    uint32_t fileOffset = 0;

    // Copy out the checksum.
    char checksum[Core::Checksum::MAX_LENGTH];
    uint32_t bytesRead = file.copyPartial(fileOffset,
                                          checksum, sizeof32(checksum));
    uint32_t checksumBytes = Core::Checksum::length(checksum, bytesRead);
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
    std::string error = Core::Checksum::verify(checksum,
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
    if (header.logId != logId) {
        PANIC("Expected log ID %lu, found %lu in %s",
              logId, header.logId, entryPath.c_str());
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
    RPC::Buffer data;
    if (header.dataChecksumLen > 0) {
        // Get and verify the data checksum.
        const char* dataChecksum =
            file.get<char>(fileOffset, header.dataChecksumLen);
        fileOffset += header.dataChecksumLen;
        if (Core::Checksum::length(dataChecksum, header.dataChecksumLen) !=
            header.dataChecksumLen) {
            PANIC("File %s corrupt", entryPath.c_str());
        }
        const void* rawData = file.get(fileOffset, header.dataLen);
        fileOffset += header.dataLen;
        error = Core::Checksum::verify(dataChecksum, rawData, header.dataLen);
        if (!error.empty()) {
            PANIC("Checksum verification failure on %s: %s",
                  entryPath.c_str(), error.c_str());
        }
        // Create the data chunk.
        data = RPC::Buffer(memcpy(new char[header.dataLen],
                                  rawData,
                                  header.dataLen),
                           header.dataLen,
                           RPC::Buffer::deleteArrayFn<char>);
    }

    // Create the LogEntry and add it to this Log.
    LogEntry entry(header.createTime,
                   std::move(data),
                   std::move(invalidations));
    entry.logId = logId;
    entry.entryId = entryId;
    entry.hasData = (header.dataChecksumLen > 0);
    entries.push_back(std::move(entry));
    if (headId == NO_ENTRY_ID || headId < entryId)
        headId = entryId;
}

void
FilesystemLog::write(const LogEntry& entry)
{
    // Calculate the data checksum.
    char dataChecksum[Core::Checksum::MAX_LENGTH];
    uint32_t dataChecksumLen = 0;
    if (entry.hasData) {
        dataChecksumLen = Core::Checksum::calculate(checksumAlgorithm.c_str(),
                                                    entry.data.getData(),
                                                    entry.data.getLength(),
                                                    dataChecksum);
    }

    // Calculate useful lengths.
    const uint32_t invalidationsBytes =
            downCast<uint32_t>(entry.invalidations.size()) *
            sizeof32(EntryId);
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
    header.logId = logId;
    header.entryId = entry.entryId;
    header.createTime = entry.createTime;
    header.invalidationsLen =
        downCast<uint32_t>(entry.invalidations.size());
    header.dataChecksumLen = dataChecksumLen;
    header.dataLen = entry.data.getLength();
    header.toBigEndian();

    // Calculate the checksum.
    char checksum[Core::Checksum::MAX_LENGTH];
    uint32_t checksumLen = Core::Checksum::calculate(
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
            { entry.data.getData(), entry.data.getLength() },
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


} // namespace LogCabin::Storage
} // namespace LogCabin
