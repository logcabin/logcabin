/* Copyright (c) 2012-2014 Stanford University
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

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "build/Protocol/Raft.pb.h"
#include "Core/Checksum.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"
#include "Core/Time.h"
#include "Core/Util.h"
#include "RPC/Buffer.h"
#include "RPC/ProtoBuf.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/SegmentedLog.h"
#include "Server/Globals.h"

namespace LogCabin {
namespace Storage {

namespace FS = FilesystemUtil;
using Core::StringUtil::format;

namespace {

uint64_t MAX_SEGMENT_SIZE = 8ul * 1024 * 1024;

enum class Encoding {
  ASCII,
  BINARY,
};

std::string
readProtoFromFile(const FS::File& file,
                  FS::FileContents& reader,
                  uint64_t* offset,
                  google::protobuf::Message* out,
                  Encoding encoding)
{
    uint64_t loffset = *offset;
    char checksum[Core::Checksum::MAX_LENGTH];
    uint64_t bytesRead = reader.copyPartial(loffset, checksum,
                                            sizeof(checksum));
    uint32_t checksumBytes = Core::Checksum::length(checksum,
                                                    uint32_t(bytesRead));
    if (checksumBytes == 0)
        return format("File %s missing checksum", file.path.c_str());
    loffset += checksumBytes;

    uint64_t dataLen;
    if (reader.copyPartial(loffset, &dataLen, sizeof(dataLen)) <
        sizeof(dataLen)) {
        return format("Record length truncated in file %s", file.path.c_str());
    }
    if (reader.getFileLength() < loffset + sizeof(dataLen) + dataLen) {
        return format("ProtoBuf truncated in file %s", file.path.c_str());
    }

    const void* checksumCoverage = reader.get(loffset,
                                              sizeof(dataLen) + dataLen);
    std::string error = Core::Checksum::verify(checksum, checksumCoverage,
                                               sizeof(dataLen) + dataLen);
    if (!error.empty()) {
        return format("Checksum verification failure on %s: %s",
                      file.path.c_str(), error.c_str());
    }
    loffset += sizeof(dataLen);
    const void* data = reader.get(loffset, dataLen);
    loffset += dataLen;

    switch (encoding) {
        case Encoding::BINARY: {
            RPC::Buffer contents(const_cast<void*>(data),
                                 Core::Util::downCast<uint32_t>(dataLen), NULL);
            if (!RPC::ProtoBuf::parse(contents, *out)) {
                return format("Failed to parse protobuf in %s",
                              file.path.c_str());
            }
            break;
        }
        case Encoding::ASCII: {
            std::string contents(static_cast<const char*>(data), dataLen);
            Core::ProtoBuf::Internal::fromString(contents, *out);
            break;
        }
    }
    *offset = loffset;
    return "";
}

/**
 * \return
 *      The total number of bytes written.
 */
uint64_t
appendProtoToFile(const google::protobuf::Message& in,
                  const FS::File& file,
                  Encoding encoding)
{
    const void* data = NULL;
    uint64_t len = 0;
    RPC::Buffer binaryContents;
    std::string asciiContents;
    switch (encoding) {
        case Encoding::BINARY: {
            RPC::ProtoBuf::serialize(in, binaryContents);
            data = binaryContents.getData();
            len = binaryContents.getLength();
            break;
        }
        case Encoding::ASCII: {
            asciiContents = Core::ProtoBuf::dumpString(in);
            data = asciiContents.data();
            len = asciiContents.length();
            break;
        }
    }
    char checksum[Core::Checksum::MAX_LENGTH];
    uint32_t checksumLen = Core::Checksum::calculate(
            "Adler32",
            {{&len, sizeof(len)}, // TODO(ongaro): htonl
             {data, len}},
             checksum);

    uint64_t start = Core::Time::rdtsc();
    ssize_t written = FS::write(file.fd, {
        {checksum, checksumLen},
        // TODO(ongaro): htonl
        {&len, sizeof(len)},
        {data, len},
    });
    if (written == -1) {
        PANIC("Failed to write to %s: %s",
              file.path.c_str(), strerror(errno));
    }
    uint64_t end = Core::Time::rdtsc();
    VERBOSE("writing us: %lu", (end - start) / 2400);
    return written;
}

RPC::Buffer
serializeProto(const google::protobuf::Message& in)
{
    RPC::Buffer binaryContents;
    RPC::ProtoBuf::serialize(in, binaryContents);
    const void* data = binaryContents.getData();
    uint64_t len = binaryContents.getLength();
    char checksum[Core::Checksum::MAX_LENGTH];
    uint32_t checksumLen = Core::Checksum::calculate(
            "Adler32",
            {{&len, sizeof(len)}, // TODO(ongaro): htonl
             {data, len}},
             checksum);

    uint64_t totalLen = checksumLen + sizeof(len) + len;
    char* buf = new char[totalLen];
    RPC::Buffer b(buf, uint32_t(totalLen), RPC::Buffer::deleteArrayFn<char[]>);
    memcpy(buf, checksum, checksumLen);
    memcpy(buf + checksumLen, &len, sizeof(len));
    memcpy(buf + checksumLen + sizeof(len), data, len);
    return b;
}


}

////////// SegmentedLog::Sync //////////
SegmentedLog::Sync::Sync(uint64_t lastIndex)
    : Log::Sync(lastIndex)
    , fds()
    , writes()
{
    // TODO(ongaro): this shouldn't be here. It's to quiet an assertion in
    // Storage/Log.h.
    completed = true;
}

void
SegmentedLog::Sync::wait()
{

    uint64_t wrote = 0;
    for (auto it = writes.begin(); it != writes.end(); ++it) {
        ssize_t written = FS::write(it->first,
                it->second.getData(),
                it->second.getLength());
        wrote += it->second.getLength();
        //ERROR("writing %u bytes", it->second.getLength());
        if (written != it->second.getLength()) {
            PANIC("Failed to write to %d: %s",
                  it->first,
                  strerror(errno));
        }
    }
    int lastDataSynced = -1;
    for (auto it = fds.begin(); it != fds.end(); ++it) {
        FS::File f(it->first, "-unknown-");
        switch (it->second) {
            case Op::FDATASYNC:
                if (lastDataSynced != it->first) {
                    NOTICE("fdatasync after %lu bytes ", wrote);
                    FS::fdatasync(f);
                    lastDataSynced = it->first;
                }
                f.release();
                break;
            case Op::FSYNC:
                //ERROR("fsync");
                FS::fsync(f);
                f.release();
                break;
            case Op::CLOSE:
                f.close();
                break;
        }
    }
}


////////// SegmentedLog public functions //////////


SegmentedLog::SegmentedLog(const FS::File& parentDir)
    : metadata()
    , dir(FS::openDir(parentDir, "log"))
    , openSegmentFile()
    , logStartIndex(1)
    , segmentsByStartIndex()
    , totalClosedSegmentBytes(0)
    , preparedSegmentsMutex()
    , preparedSegmentConsumed()
    , preparedSegmentProduced()
    , segmentPreparerExiting(false)
    , preparedSegments()
    , segmentFilenameCounter(1)
    , currentSync(new SegmentedLog::Sync(0))
    , segmentPreparer()
{
    std::vector<Segment> segments = readSegmentFilenames();

    SegmentedLogMetadata::Metadata metadata1;
    SegmentedLogMetadata::Metadata metadata2;
    std::string error1 = readMetadata("metadata1", metadata1);
    std::string error2 = readMetadata("metadata2", metadata2);
    if (error1.empty() && error2.empty()) {
        if (metadata1.version() > metadata2.version())
            metadata = metadata1;
        else
            metadata = metadata2;
    } else if (error1.empty()) {
        metadata = metadata1;
    } else if (error2.empty()) {
        metadata = metadata2;
    } else {
        // Brand new servers won't have metadata.
        WARNING("Error reading metadata1: %s", error1.c_str());
        WARNING("Error reading metadata2: %s", error2.c_str());
        if (!segments.empty()) {
            PANIC("No readable metadata file but found segments in %s",
                  dir.path.c_str());
        }
        metadata.set_entries_start(logStartIndex);
    }

    logStartIndex = metadata.entries_start();
    Log::metadata = metadata.raft_metadata();
    // Write both metadata files
    updateMetadata();
    updateMetadata();
    FS::fsync(dir); // in case metadata files didn't exist


    // TODO(ongaro): ignore errors below start index
    for (auto it = segments.begin(); it != segments.end(); ++it) {
        Segment& segment = *it;
        bool keep = loadSegment(segment);
        if (keep) {
            assert(!it->isOpen);
            segmentsByStartIndex.insert({segment.startIndex,
                                         std::move(segment)});
        }
    }

    // Check to make sure no entry is present in more than one segment.
    if (!segmentsByStartIndex.empty()) {
        uint64_t nextIndex = segmentsByStartIndex.begin()->first;
        for (auto it = segmentsByStartIndex.begin();
             it != segmentsByStartIndex.end();
             ++it) {
            Segment& segment = it->second;
            if (nextIndex < segment.startIndex) {
                PANIC("Did not find segment containing entries "
                      "%16lx to %16lx (inclusive)",
                      nextIndex, segment.startIndex - 1);
            } else if (segment.startIndex < nextIndex) {
                PANIC("Segment %s contains duplicate entries "
                      "%16lx to %16lx (inclusive)",
                      segment.filename.c_str(),
                      segment.startIndex, nextIndex - 1);
            }
            nextIndex = segment.endIndex + 1;
        }
    }

    { // Open a segment to write new entries into.
        Segment newSegment;
        newSegment.isOpen = true;
        newSegment.startIndex = getLastLogIndex() + 1;
        newSegment.endIndex = newSegment.startIndex - 1;
        newSegment.bytes = 0;
        std::pair<std::string, FS::File> prepared =
            prepareNewSegment();
        newSegment.filename = prepared.first;
        openSegmentFile = std::move(prepared.second);
        segmentsByStartIndex.insert({newSegment.startIndex, newSegment});
    }

    // Launch the segment preparer thread so that we'll have a source for
    // additional new segments.
    segmentPreparer = std::thread(&SegmentedLog::segmentPreparerMain, this);

    // Remove any unneeded segments and entries.
    truncatePrefix(logStartIndex);

    checkInvariants();
}

SegmentedLog::~SegmentedLog()
{
    {
        Sync sync(0);
        closeSegment(sync);
        sync.wait();
    }

    // Stop preparing segments and delete the extras.
    {
        std::unique_lock<Core::Mutex> lockGuard(preparedSegmentsMutex);
        segmentPreparerExiting = true;
        preparedSegmentConsumed.notify_all();
    }
    if (segmentPreparer.joinable())
        segmentPreparer.join();
    while (!preparedSegments.empty()) {
        FS::removeFile(dir, preparedSegments.front().first);
        preparedSegments.pop_front();
    }
    FS::fsync(dir);
}

std::pair<uint64_t, uint64_t>
SegmentedLog::append(const std::vector<const Entry*>& entries)
{
    Segment* openSegment = &getOpenSegment();
    uint64_t startIndex = openSegment->endIndex + 1;
    uint64_t index = startIndex;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        // TODO(ongaro): Awful break of abstraction here, this knows way too
        // much about appendProtoToFile
        // TODO(ongaro): this also uses it->ByteSize() before it has an index
        uint64_t testBytes = (openSegment->bytes +
             Core::Checksum::MAX_LENGTH +
             8 +
             16 +
             (*it)->ByteSize());
        if (testBytes >
            MAX_SEGMENT_SIZE) {
            NOTICE("roll segments");
            closeSegment(*currentSync);
            openNewSegment();
            openSegment = &getOpenSegment();
        }
        uint64_t offset = openSegment->bytes;
        openSegment->entries.emplace_back(offset);
        openSegment->entries.back().entry = **it;
        openSegment->entries.back().entry.set_index(index);
#if 0
        uint64_t actBytes = appendProtoToFile(openSegment->entries.back().entry,
                                              openSegmentFile,
                                              Encoding::BINARY);
#else
        RPC::Buffer buf = serializeProto(openSegment->entries.back().entry);
        uint64_t actBytes = buf.getLength();
        currentSync->writes.push_back({openSegmentFile.fd, std::move(buf)});
#endif
        if (actBytes > testBytes)
            PANIC("actBytes: %lu, testBytes: %lu", actBytes, testBytes);
        openSegment->bytes += actBytes;
        ++openSegment->endIndex;
        ++index;
    }
    currentSync->fds.push_back({openSegmentFile.fd, Sync::Op::FDATASYNC});
    currentSync->lastIndex = getLastLogIndex();
    checkInvariants();
    return {startIndex, getLastLogIndex()};
}

const SegmentedLog::Entry&
SegmentedLog::getEntry(uint64_t index) const
{
    if (index < getLogStartIndex() ||
        index > getLastLogIndex()) {
        PANIC("Attempted to access entry %lu outside of log "
              "(start index is %lu, last index is %lu)",
              index, getLogStartIndex(), getLastLogIndex());
    }
    auto it = segmentsByStartIndex.upper_bound(index);
    --it;
    const Segment& segment = it->second;
    assert(segment.startIndex <= index);
    assert(index <= segment.endIndex);
    return segment.entries.at(index - segment.startIndex).entry;
}

uint64_t
SegmentedLog::getLogStartIndex() const
{
    return logStartIndex;
}

uint64_t
SegmentedLog::getLastLogIndex() const
{
    // Although it's a class invariant that there's always an open segment,
    // it's convenient to be able to call this as a helper function when there
    // are no segments.
    if (segmentsByStartIndex.empty())
        return logStartIndex - 1;
    else
        return getOpenSegment().endIndex;
}

uint64_t
SegmentedLog::getSizeBytes() const
{
    return totalClosedSegmentBytes + getOpenSegment().bytes;
}

std::unique_ptr<Log::Sync>
SegmentedLog::takeSync()
{
    std::unique_ptr<SegmentedLog::Sync> other(
            new SegmentedLog::Sync(getLastLogIndex()));
    std::swap(other, currentSync);
    return std::move(other);
}

void
SegmentedLog::truncatePrefix(uint64_t newStartIndex)
{
    if (newStartIndex <= logStartIndex)
        return;

    logStartIndex = newStartIndex;
    // update metadata before removing files in case of interruption
    updateMetadata();

    while (!segmentsByStartIndex.empty()) {
        Segment& segment = segmentsByStartIndex.begin()->second;
        if (logStartIndex <= segment.endIndex)
            break;
        NOTICE("Deleting unneeded segment %s",
               segment.filename.c_str());
        FS::removeFile(dir, segment.filename);
        if (segment.isOpen)
            openSegmentFile.close();
        else
            totalClosedSegmentBytes -= segment.bytes;
        segmentsByStartIndex.erase(segmentsByStartIndex.begin());
    }

    if (segmentsByStartIndex.empty())
        openNewSegment();
    checkInvariants();
}

void
SegmentedLog::truncateSuffix(uint64_t newEndIndex)
{
    if (newEndIndex >= getLastLogIndex())
        return;

    {
        Segment& openSegment = getOpenSegment();
        if (newEndIndex >= openSegment.startIndex) {
            // Update in-memory segment
            uint64_t i = newEndIndex + 1 - openSegment.startIndex;
            openSegment.bytes = openSegment.entries.at(i).offset;
            openSegment.entries.erase(
                openSegment.entries.begin() + i,
                openSegment.entries.end());
            openSegment.endIndex = newEndIndex;
            // Truncate and close the open segment, and open a new one.
            Sync sync(0);
            closeSegment(sync);
            sync.wait();
            openNewSegment();
            checkInvariants();
            return;
        }
    }

    { // Remove the open segment.
        Segment& openSegment = getOpenSegment();
        openSegment.endIndex = openSegment.startIndex - 1;
        openSegment.bytes = 0;
        Sync sync(0);
        closeSegment(sync);
        sync.wait();
    }

    // Remove and/or truncate closed segments.
    while (!segmentsByStartIndex.empty()) {
        auto it = segmentsByStartIndex.rbegin();
        Segment& segment = it->second;
        if (segment.endIndex == newEndIndex)
            break;
        if (segment.startIndex > newEndIndex) { // remove segment
            FS::removeFile(dir, segment.filename);
            FS::fsync(dir);
            totalClosedSegmentBytes -= segment.bytes;
            segmentsByStartIndex.erase(segment.startIndex);
        } else if (segment.endIndex > newEndIndex) { // truncate segment
            // Update in-memory segment
            uint64_t i = newEndIndex + 1 - segment.startIndex;
            uint64_t newBytes = segment.entries.at(i).offset;
            totalClosedSegmentBytes -= (segment.bytes - newBytes);
            segment.bytes = newBytes;
            segment.entries.erase(
                segment.entries.begin() + i,
                segment.entries.end());
            segment.endIndex = newEndIndex;

            // Rename the file
            std::string newFilename = format("%016lx-%016lx",
                                             segment.startIndex,
                                             segment.endIndex);
            FS::rename(dir, segment.filename,
                       dir, newFilename);
            FS::fsync(dir);
            segment.filename = newFilename;

            // Truncate the file
            FS::File f = FS::openFile(dir, segment.filename, O_WRONLY);
            FS::truncate(f, segment.bytes);
            FS::fsync(f);
        }
    }

    // Reopen a segment (so that we can write again)
    openNewSegment();
    checkInvariants();
}

void
SegmentedLog::updateMetadata()
{
    if (Log::metadata.ByteSize() == 0)
        metadata.clear_raft_metadata();
    else
        *metadata.mutable_raft_metadata() = Log::metadata;
    metadata.set_entries_start(logStartIndex);
    metadata.set_version(metadata.version() + 1);
    std::string filename;
    if (metadata.version() % 2 == 1) {
        filename = "metadata1";
    } else {
        filename = "metadata2";
    }

    FS::File file = FS::openFile(dir, filename, O_CREAT|O_WRONLY|O_TRUNC);
    appendProtoToFile(metadata, file, Encoding::BINARY);
    FS::fsync(file);
}


////////// SegmentedLog initialization helper functions //////////


std::vector<SegmentedLog::Segment>
SegmentedLog::readSegmentFilenames()
{
    std::vector<Segment> segments;
    std::vector<std::string> filenames = FS::ls(dir);
    for (auto it = filenames.begin(); it != filenames.end(); ++it) {
        const std::string& filename = *it;
        if (filename == "metadata1" ||
            filename == "metadata2" ||
            filename == "unknown") {
            continue;
        }
        Segment segment;
        segment.filename = filename;
        segment.bytes = 0;
        { // Closed segment: xxx-yyy
            uint64_t startIndex = 1;
            uint64_t endIndex = 0;
            unsigned bytesConsumed;
            int matched = sscanf(filename.c_str(), "%016lx-%016lx%n", // NOLINT
                                 &startIndex, &endIndex,
                                 &bytesConsumed);
            if (matched == 2 && bytesConsumed == filename.length()) {
                segment.isOpen = false;
                segment.startIndex = startIndex;
                segment.endIndex = endIndex;
                segments.push_back(segment);
                continue;
            }
        }

        { // Open segment: open-xxx
            uint64_t counter;
            unsigned bytesConsumed;
            int matched = sscanf(filename.c_str(), "open-%lu%n", // NOLINT
                                 &counter,
                                 &bytesConsumed);
            if (matched == 1 && bytesConsumed == filename.length()) {
                segment.isOpen = true;
                segment.startIndex = ~0UL;
                segment.endIndex = ~0UL - 1;
                segments.push_back(segment);
                if (segmentFilenameCounter <= counter) {
                    segmentFilenameCounter = counter + 1;
                }
                continue;
            }
        }

        // Neither
        WARNING("%s doesn't look like a valid segment filename (from %s)",
                filename.c_str(),
                (dir.path + "/" + filename).c_str());
    }
    return segments;
}


std::string
SegmentedLog::readMetadata(const std::string& filename,
                            SegmentedLogMetadata::Metadata& metadata) const
{
    FS::File file = FS::tryOpenFile(dir, filename, O_RDONLY);
    if (file.fd == -1) {
        return format("Could not open %s/%s: %s",
                      dir.path.c_str(), filename.c_str(), strerror(errno));
    }
    FS::FileContents reader(file);
    uint64_t offset = 0;
    return readProtoFromFile(file, reader, &offset, &metadata,
                             Encoding::BINARY);
}

bool
SegmentedLog::loadSegment(Segment& segment)
{
    FS::File file = FS::openFile(dir, segment.filename, O_RDWR);
    FS::FileContents reader(file);
    uint64_t offset = 0;
    if (segment.isOpen) {
        while (offset < reader.getFileLength()) {
            segment.entries.emplace_back(offset);
            std::string error = readProtoFromFile(file, reader, &offset,
                                                  &segment.entries.back().entry,
                                                  Encoding::BINARY);
            if (!error.empty()) {
                segment.entries.pop_back();
                WARNING("Could not read entry in log segment %s "
                        "(offset %lu), probably because it was being "
                        "written when the server crashed. Discarding the "
                        "remainder of the file (%lu bytes). Error was: %s",
                        segment.filename.c_str(),
                        offset,
                        reader.getFileLength() - offset,
                        error.c_str());
                FS::truncate(file, offset);
                FS::fsync(file);
                break;
            }
        }
        if (segment.entries.empty()) {
            FS::removeFile(dir, segment.filename);
            FS::fsync(dir);
            return false;
        } else {
            segment.bytes = offset;
            totalClosedSegmentBytes += segment.bytes;
            segment.isOpen = false;
            segment.startIndex = segment.entries.front().entry.index();
            segment.endIndex = segment.entries.back().entry.index();
            std::string newFilename = format("%016lx-%016lx",
                                             segment.startIndex,
                                             segment.endIndex);
            FS::rename(dir, segment.filename,
                       dir, newFilename);
            FS::fsync(dir);
            segment.filename = newFilename;
            return true;
        }
    } else { /* !segment.isOpen */
        for (uint64_t index = segment.startIndex;
             index <= segment.endIndex;
             ++index) {
            segment.entries.emplace_back(offset);
            std::string error;
            if (offset > reader.getFileLength()) {
                error = "File too short";
            } else {
                error = readProtoFromFile(file, reader, &offset,
                                          &segment.entries.back().entry,
                                          Encoding::BINARY);
            }
            if (!error.empty()) {
                PANIC("Could not read entry %lu in log segment %s "
                      "(offset %lu). This indicates the file was somehow "
                      "corrupted. Error was: %s",
                      index,
                      segment.filename.c_str(),
                      offset,
                      error.c_str());
            }
        }
        if (offset < reader.getFileLength()) {
            WARNING("Ignoring %lu bytes at the end of closed segment %s. "
                    "This can happen if the server crashed while truncating "
                    "the segment.",
                    reader.getFileLength() - offset, segment.filename.c_str());
        }
        segment.bytes = offset;
        totalClosedSegmentBytes += segment.bytes;
        return true;
    }
}


////////// SegmentedLog normal operation helper functions //////////


void
SegmentedLog::checkInvariants()
{
#if DEBUG
    assert(openSegmentFile.fd >= 0);
    assert(!segmentsByStartIndex.empty());
    assert(logStartIndex >= segmentsByStartIndex.begin()->second.startIndex);
    assert(logStartIndex <= segmentsByStartIndex.begin()->second.endIndex + 1);
    uint64_t closedBytes = 0;
    for (auto it = segmentsByStartIndex.begin();
         it != segmentsByStartIndex.end();
         ++it) {
        auto next = it;
        ++next;
        Segment& segment = it->second;
        assert(it->first == segment.startIndex);
        assert(segment.startIndex > 0);
        assert(segment.bytes <= MAX_SEGMENT_SIZE);
        assert(segment.entries.size() ==
               segment.endIndex + 1 - segment.startIndex);
        for (uint64_t i = 0; i < segment.entries.size(); ++i) {
            assert(segment.entries.at(i).entry.index() ==
                   segment.startIndex + i);
        }
        if (next == segmentsByStartIndex.end()) {
            assert(segment.isOpen);
            assert(segment.endIndex >= segment.startIndex - 1);
            assert(Core::StringUtil::startsWith(segment.filename, "open-"));
        } else {
            assert(!segment.isOpen);
            assert(segment.endIndex >= segment.startIndex);
            assert(next->second.startIndex == segment.endIndex + 1);
            assert(segment.bytes > 0);
            closedBytes += segment.bytes;
            assert(segment.filename == format("%016lx-%016lx",
                                              segment.startIndex,
                                              segment.endIndex));
        }
    }
    assert(closedBytes == totalClosedSegmentBytes);
#endif
}

void
SegmentedLog::closeSegment(Sync& sync)
{
    if (openSegmentFile.fd < 0)
        return;
    Segment& openSegment = getOpenSegment();
    if (openSegment.startIndex > openSegment.endIndex) {
        // Segment is empty; just remove it.
        WARNING("Removing empty segment");
        openSegmentFile.close();
        FS::removeFile(dir, openSegment.filename);
        sync.fds.push_back({dir.fd, Sync::Op::FSYNC});
        segmentsByStartIndex.erase(openSegment.startIndex);
        return;
    }
    // Rename the file.
    std::string newFilename = format("%016lx-%016lx",
                                     openSegment.startIndex,
                                     openSegment.endIndex);
    FS::rename(dir, openSegment.filename,
               dir, newFilename);
    sync.fds.push_back({dir.fd, Sync::Op::FSYNC});
    openSegment.filename = newFilename;

    // truncate away any extra 0 bytes at the end from when
    // MAX_SEGMENT_SIZE was allocated
    FS::truncate(openSegmentFile, openSegment.bytes);
    sync.fds.push_back({openSegmentFile.fd, Sync::Op::FSYNC});
    sync.fds.push_back({openSegmentFile.release(), Sync::Op::CLOSE});

    openSegment.isOpen = false;
    totalClosedSegmentBytes += openSegment.bytes;
}

SegmentedLog::Segment&
SegmentedLog::getOpenSegment()
{
    assert(!segmentsByStartIndex.empty());
    return segmentsByStartIndex.rbegin()->second;
}

const SegmentedLog::Segment&
SegmentedLog::getOpenSegment() const
{
    assert(!segmentsByStartIndex.empty());
    return segmentsByStartIndex.rbegin()->second;
}

void
SegmentedLog::openNewSegment()
{
    assert(openSegmentFile.fd < 0);
    assert(segmentsByStartIndex.empty() ||
           !segmentsByStartIndex.rbegin()->second.isOpen);

    Segment newSegment;
    newSegment.isOpen = true;
    newSegment.startIndex = getLastLogIndex() + 1;
    newSegment.endIndex = newSegment.startIndex - 1;
    newSegment.bytes = 0;
    {
        std::unique_lock<Core::Mutex> lockGuard(preparedSegmentsMutex);
        while (preparedSegments.empty()) {
            WARNING("Prepared segment not ready, having to wait on it. "
                    "This is perfectly safe but bad for performance.");
            preparedSegmentProduced.wait(lockGuard);
            if (!preparedSegments.empty())
                WARNING("Done waiting, prepared segment now ready");
        }
        newSegment.filename = std::move(preparedSegments.front().first);
        openSegmentFile = std::move(preparedSegments.front().second);
        preparedSegments.pop_front();
        preparedSegmentConsumed.notify_all();
    }
    segmentsByStartIndex.insert({newSegment.startIndex, newSegment});
}


////////// SegmentedLog segment preparer thread functions //////////


std::pair<std::string, FS::File>
SegmentedLog::prepareNewSegment()
{
    std::string filename = format("open-%lu", ++segmentFilenameCounter);
    FS::File segment = FS::openFile(dir, filename,
                                    O_CREAT|O_WRONLY|O_EXCL);
    FS::allocate(segment, 0, MAX_SEGMENT_SIZE);
    FS::fsync(segment);
    FS::fsync(dir);
    return {std::move(filename), std::move(segment)};
}

void
SegmentedLog::segmentPreparerMain()
{
    std::unique_lock<Core::Mutex> lockGuard(preparedSegmentsMutex);
    Core::ThreadId::setName("SegmentPreparer");
    while (!segmentPreparerExiting) {
        if (preparedSegments.size() < 3) {
            std::pair<std::string, FS::File> segment;
            {
                // release lock for concurrency
                Core::MutexUnlock<Core::Mutex> unlockGuard(lockGuard);
                segment = prepareNewSegment();
            }
            preparedSegments.push_back(std::move(segment));
            preparedSegmentProduced.notify_all();
            continue;
        }
        preparedSegmentConsumed.wait(lockGuard);
    }
}

} // namespace LogCabin::Storage
} // namespace LogCabin
