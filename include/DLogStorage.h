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
 * The interface into the storage library.
 */

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Ref.h"

#ifndef DLOGSTORAGE_H
#define DLOGSTORAGE_H

namespace DLog {

// TODO(ongaro): These probably need to go elsewhere.
typedef uint64_t LogId;
typedef uint64_t EntryId;
static const LogId NO_LOG_ID = ~0ULL;
static const EntryId NO_ENTRY_ID = ~0ULL;
typedef uint64_t TimeStamp;

namespace Storage {

/**
 * An immutable, reference-counted buffer.
 */
class Chunk {
  public:
    /**
     * Create and return a new chunk.
     * \param data
     *      Contents with which to initialize the chunk.
     * \param length
     *      The number of bytes in data.
     * \return
     *      The newly created chunk.
     */
    static Ref<Chunk> makeChunk(const void* data, uint32_t length);
    /// Return a pointer to the first byte of data for this chunk.
    const void* getData() const;
    /// Return the number of bytes that make up data.
    uint32_t getLength() const;

  private:
    /**
     * Constructor for Chunk, called only by makeChunk().
     * The caller must ensure that length bytes are available directly
     * following this instance in which to store data.
     * \param data
     *      Data to be copied into the new chunk.
     * \param length
     *      The number of bytes in data.
     */
    Chunk(const void* data, uint32_t length);

    /**
     * Reference count for RefHelper<Chunk> to use.
     */
    uint32_t refCount;
    /**
     * The number of bytes that make up data.
     */
    const uint32_t length;
    /**
     * Points to the first byte of memory following this class.
     * Must be the last member of this class.
     */
    const char data[0];

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    // specialized because Chunks are allocated with malloc
    friend class RefHelper<Chunk>;
};

/**
 * An empty chunk that is signifies the user did not supply data.
 */
extern Ref<Chunk> NO_DATA;

/**
 * These are returned by reads to a Log.
 */
class LogEntry {
  public:
    /**
     * Construct an entry with user-supplied data and optional invalidations.
     */
    LogEntry(LogId logId, EntryId entryId,
             TimeStamp createTime,
             Ref<Chunk> data,
             const std::vector<EntryId>& invalidations =
                std::vector<EntryId>());

    /**
     * Construct an entry that only has invalidations.
     */
    LogEntry(LogId logId, EntryId entryId,
             TimeStamp createTime,
             const std::vector<EntryId>& invalidations);

    /**
     * The log this entry is or was a part of.
     */
    LogId logId;

    /**
     * The log together with this field uniquely identifies the entry.
     */
    EntryId entryId;

    /**
     * The time this entry was created on the leader.
     */
    TimeStamp createTime;

    /**
     * A list of entry IDs that this entry invalidates.
     */
    std::vector<EntryId> invalidations;

    /**
     * The user-supplied data for the log entry.
     * If this is set to NO_DATA, the user did not supply any data for the log
     * entry (that's different than a length of 0).
     */
    Ref<Chunk> data;
};

/**
 * An interface that each storage module implements to read and manipulate an
 * individual log.
 */
class Log {
  public:

    /**
     * See beginSync().
     */
    class AppendCallback {
      public:
        virtual ~AppendCallback() {}
        virtual void appended(LogEntry entry) = 0;
    };

    /**
     * See addDestructorCallback().
     */
    class DestructorCallback {
      public:
        virtual ~DestructorCallback() {}
        virtual void destructorCallback(LogId logId) = 0;
    };

    /**
     * Constructor.
     */
    Log();

    /**
     * Destructor. Calls the destructorCompletions.
     */
    virtual ~Log();

    /**
     * Return this logs ID.
     */
    virtual LogId getLogId() = 0;

    /**
     * Return the ID for the entry at the head of the log.
     * This will return NO_ENTRY_ID if the log is empty.
     * It will return the entry ID even for data that has not been fully
     * written to durable storage.
     */
    virtual EntryId getLastId() = 0;

    /**
     * Return a copy of the entries from 'start' to the head of the log.
     * \param start
     *      The entry at which to start reading.
     * \return
     *      The entries starting at and including 'start' through head of the
     *      log.
     */
    virtual std::deque<LogEntry> readFrom(EntryId start) = 0;

    /**
     * Append a log entry (data and/or invalidations).
     * \param[in,out] entry
     *      The entry to append. Its EntryId will be set upon the return of
     *      this function. A copy of this entry will be passed to
     *      appendCompletion once the entry has been written to durable
     *      storage.
     * \param appendCompletion
     *      Called once entry has may be considered durable.
     */
    virtual void append(LogEntry& entry,
                        std::unique_ptr<AppendCallback> appendCompletion) = 0;

    /**
     * Register a callback to be called when this class is destroyed.
     */
    void addDestructorCallback(std::unique_ptr<DestructorCallback>
                                destructorCompletion);

  private:
    std::vector<std::unique_ptr<DestructorCallback>> destructorCompletions;
    uint32_t refCount;
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    friend class RefHelper<Log>;
};

/**
 * An interface for managing logs on durable storage.
 * This is a bit of a dangerous interface which is only used by LogManager.
 */
class StorageModule {
  private:
    /**
     * See deleteLog().
     */
    class DeleteCallback {
      public:
        virtual ~DeleteCallback() {}
        virtual void deleted(LogId logId) = 0;
    };

  public:
    virtual ~StorageModule() {}

  private:
    /**
     * Scan the logs on the durable storage and return handles to each one.
     * \warning
     *      The caller should be careful to call this only once, since having
     *      multiple Log objects that refer to the same underlying storage is
     *      likely to result in data corruption.
     */
    virtual std::vector<Ref<Log>> getLogs() = 0;

    /**
     * Create a new log.
     * Making this method asynchronous is probably unnecessarily pain.
     *
     * This need not be atomic but must be idempotent. After even a partial
     * create, getLogs() must return this log and deleteLog() must be able to
     * delete any storage resources allocated to this log.
     *
     * \param logId
     *      The caller asserts that this ID has never been used before.
     * \return
     *      A handle to the newly constructed log object.
     */
    virtual Ref<Log> createLog(LogId logId) = 0;

    /**
     * Delete a log from stable storage.
     *
     * This need not be atomic but must be idempotent. After a successful
     * delete, getLogs() may not return this log ever again. If the delete
     * fails, however, getLogs() must return this log.
     *
     * \param logId
     *      The caller asserts that no Log object exists for this ID.
     * \param deleteCompletion
     *      Called once the delete completes.
     */
    virtual void deleteLog(LogId logId,
                           std::unique_ptr<DeleteCallback>
                                            deleteCompletion) = 0;

    StorageModule(const StorageModule&) = delete;
    StorageModule& operator=(const StorageModule&) = delete;
};

} // namespace DLog::Storage

// specialization because Chunks are allocated with malloc
template<>
class RefHelper<Storage::Chunk>;

} // namespace DLog

#endif /* DLOGSTORAGE_H */
