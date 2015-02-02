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

#include <deque>
#include <thread>
#include <vector>

#include "build/Storage/SegmentedLog.pb.h"
#include "Core/ConditionVariable.h"
#include "Core/Mutex.h"
#include "RPC/Buffer.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/Log.h"

#ifndef LOGCABIN_STORAGE_SEGMENTEDLOG_H
#define LOGCABIN_STORAGE_SEGMENTEDLOG_H

namespace LogCabin {
namespace Storage {


/**
 * This class persists a log on the filesystem efficiently.
 *
 * The log entries on disk are stored in a series of files called segments, and
 * each segment is about 8MB in size. Thus, most small appends do not need to
 * TODO: where's the rest of this sentence
 *
 *
 * The disk files consist of metadata files, closed segments, and open
 * segments. Metadata files are used to track Raft metadata such as the
 * server's current term but also the log's start index. Segments contain
 * contiguous entries that are part of the log. Closed segments are never
 * written to again (but may be truncated if a suffix of the log is truncated).
 * Open segments are where newly appended entries go. Once an open segment
 * reaches MAX_SEGMENT_SIZE, it is closed and a new one is used.
 *
 * Metadata files are named "metadata1" and "metadata2". The code alternates
 * between these so that there is always at least one readable metadata file.
 * On boot, the readable metadata file with the higher version number is used.
 *
 * Closed segments are named by the format string "%016lx-%016lx" with their
 * start and end indexes, both inclusive. Closed segments always contain at
 * least one entry; the end index is always at least as large as the start
 * index. Closed segment files may occasionally include data past their
 * filename's end index (these are ignored but a WARNING is issued). This can
 * happen if the suffix of the segment is truncated and a crash occurs at an
 * inopportune time (the segment file is first renamed, then truncated, and
 * this situation occurs when a crash occurs in between).
 *
 * Open segments are named by the format string "open-%lu" with a unique
 * number. These should not exist when the server shuts down cleanly, but they
 * exist while the server is running and may be left around during a crash.
 * Open segments either contain entries which come after the last closed
 * segment or are full of zeros. When the server crashes while appending to an
 * open segment, the end of that file may be corrupt. We can't distinguish
 * between a corrupt file and a partially written entry. The code assumes it's
 * a partially written entry, issues a WARNING, and ignores it.
 *
 */
class SegmentedLog : public Log {

  public:

    explicit SegmentedLog(const Storage::FilesystemUtil::File& parentDir);
    ~SegmentedLog();

    // Methods implemented from Log interface
    std::pair<uint64_t, uint64_t>
    append(const std::vector<const Entry*>& entries);
    const Entry& getEntry(uint64_t) const;
    uint64_t getLogStartIndex() const;
    uint64_t getLastLogIndex() const;
    uint64_t getSizeBytes() const;
    std::unique_ptr<Log::Sync> takeSync();
    void truncatePrefix(uint64_t firstIndex);
    void truncateSuffix(uint64_t lastIndex);
    void updateMetadata();

  private:
    class Sync : public Log::Sync {
      public:
        enum class Op {
            FDATASYNC,
            FSYNC,
            CLOSE,
        };
        explicit Sync(uint64_t lastIndex);
        void wait();
        /**
         * List of file descriptors that should be synced or closed on wait().
         */
        std::vector<std::pair<int, Op>> fds;
        std::deque<std::pair<int, RPC::Buffer>> writes;
    };

    /**
     * An open or closed segment. These are stored in #segmentsByStartIndex.
     */
    struct Segment {
        /**
         * Describes a log entry record within a segment.
         */
        struct Record {
            /**
             * Constructor.
             */
            explicit Record(uint64_t offset)
                : offset(offset)
                , entry() {
            }

            /**
             * Byte offset in the file where the entry begins.
             */
            uint64_t offset;

            /**
             * The entry itself.
             */
            Log::Entry entry;
        };

        /**
         * Default constructor. Sets the segment to invalid.
         */
        Segment()
            : isOpen(false)
            , startIndex(~0UL)
            , endIndex(~0UL - 1)
            , bytes(0)
            , filename("--invalid--")
            , entries() {
        }

        /**
         * True for the open segment, false for closed segments.
         */
        bool isOpen;
        /**
         * The index of the first entry in the segment. If the segment is open
         * and empty, this may not exist yet and will be #logStartIndex.
         */
        uint64_t startIndex;
        /**
         * The index of the last entry in the segment, or #startIndex - 1 if
         * the segment is open and empty.
         */
        uint64_t endIndex;
        /**
         * Size in bytes of the valid entries stored in the file.
         */
        uint64_t bytes;
        /**
         * The name of the file within #dir containing this segment.
         */
        std::string filename;
        /**
         * The entries in this segment, from startIndex to endIndex, inclusive.
         */
        std::deque<Record> entries;

    };


    ////////// initialization helper functions //////////

    /**
     * List the files in #dir and create Segment objects for any of them that
     * look like segments. This is only used during initialization. These
     * segments are passed through #loadSegment() next.
     * Also updates #segmentFilenameCounter.
     * \return
     *      Partially initialized Segment objects, one per discovered filename.
     */
    std::vector<Segment> readSegmentFilenames();

    /**
     * Read a metadata file from disk. This is only used during initialization.
     * \param filename
     *      Filename within #dir to attempt to open and read.
     * \param[out] metadata
     *      Where the contents of the file end up.
     * \return
     *      Empty string if the file was read successfully, or a string
     *      describing the error that occurred.
     */
    std::string readMetadata(const std::string& filename,
                             SegmentedLogMetadata::Metadata& metadata) const;

    /**
     * Read the given segment from disk, issuing PANICs and WARNINGs
     * appropriately, and closing any open segments. This is only used during
     * initialization.
     *
     * For open segments, reads up through the end of the file or the last
     * entry with a valid checksum. If any valid entries are read, the segment
     * is truncated and closed. Otherwise, it is removed.
     *
     * For closed segments, reads every entry described in the filename, and
     * PANICs if any of those can't be read.
     *
     * \param[in,out] segment
     *      Segment to read from disk.
     * \return
     *      True if the segment is valid; false if it has been removed entirely
     *      from disk.
     */
    bool loadSegment(Segment& segment);


    ////////// normal operation helper functions //////////

    /**
     * Run through a bunch of assertions of class invariants (for debugging).
     */
    void checkInvariants();

    /**
     * Close the open segment if one is open. This removes the open segment if
     * it is empty, or closes it otherwise. Since it's a class invariant that
     * there is always an open segment, the caller should open a new segment
     * after calling this (unless it's shutting down).
     */
    void closeSegment(Sync& sync);

    /**
     * Return a reference to the current open segment (the one that new writes
     * should go into). Crashes if there is no open segment (but it's an
     * invariant of this class to maintain one).
     */
    Segment& getOpenSegment();
    const Segment& getOpenSegment() const;

    /**
     * Close the current open segment, if any, and open a new one. This is
     * called when #append() needs more space but also when the end of the log
     * is truncated with #truncatePrefix() or #truncateSuffix().
     */
    void openNewSegment();


    ////////// segment preparer thread functions //////////

    /**
     * Opens a file for a new segment and allocates its space on disk. This may
     * only be called from the #segmentPreparer thread, as it accesses
     * #segmentFilenameCounter.
     * \return
     *      Filename and writable OS-level file.
     */
    std::pair<std::string, Storage::FilesystemUtil::File> prepareNewSegment();

    /**
     * The main function for the #segmentPreparer thread.
     */
    void segmentPreparerMain();


    /**
     * The metadata this class mintains. This should be combined with the
     * superclass's metadata when being written out to disk.
     */
    SegmentedLogMetadata::Metadata metadata;

    /**
     * The directory containing every file this log creates.
     */
    Storage::FilesystemUtil::File dir;

    /**
     * A writable OS-level file that contains the entries for the current open
     * segment. It is a class invariant that this is always a valid file.
     */
    Storage::FilesystemUtil::File openSegmentFile;

    /**
     * The index of the first entry in the log, see getLogStartIndex().
     */
    uint64_t logStartIndex;

    /**
     * Ordered map of all closed segments and the open segment, indexed by the
     * startIndex of each segment. This is used to support all the key
     * operations, such as looking up an entry and truncation.
     */
    std::map<uint64_t, Segment> segmentsByStartIndex;

    /**
     * The total number of bytes occupied by the closed segments on disk.
     * Used to calculate getSizeBytes() efficiently.
     */
    uint64_t totalClosedSegmentBytes;

    /**
     * Mutual exclusion for #segmentPreparerExiting and #preparedSegments.
     */
    Core::Mutex preparedSegmentsMutex;
    /**
     * Notified when the log uses a prepared segment as a new open segment, or
     * when #segmentPreparerExiting becomes true.
     */
    Core::ConditionVariable preparedSegmentConsumed;
    /**
     * Notified when #segmentPreparer adds a new prepared segment to
     * #preparedSegments.
     */
    Core::ConditionVariable preparedSegmentProduced;
    /**
     * Initially false; set to true by ~SegmentedLog() when #segmentPreparer
     * should exit for a clean shutdown. #preparedSegmentConsumed is also
     * notified when this is set to true.
     */
    bool segmentPreparerExiting;
    /**
     * A queue of segments that is available for the log to use as future open
     * segments. segmentPreparerMain() bounds the size of this queue.
     */
    std::deque<std::pair<std::string, Storage::FilesystemUtil::File>>
        preparedSegments;
    /**
     * Used to assign filenames to open segments (which is done before the
     * indexes they contain is known). This number is larger than all
     * previously known numbers so that it can be assigned to new files.
     * It's possible that numbers are reused across reboots.
     * This may only be accessed from the #segmentPreparer thread.
     */
    uint64_t segmentFilenameCounter;
    std::unique_ptr<SegmentedLog::Sync> currentSync;
    /**
     * Opens files, allocates the to full size, and places them on
     * #preparedSegments for the log to use.
     */
    std::thread segmentPreparer;
};

} // namespace LogCabin::Storage
} // namespace LogCabin

#endif /* LOGCABIN_STORAGE_SEGMENTEDLOG_H */
