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

/**
 * \file
 * This file declares the interface for LogCabin's client library.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#ifndef LOGCABIN_CLIENT_CLIENT_H
#define LOGCABIN_CLIENT_CLIENT_H

namespace LogCabin {
namespace Client {

class ClientImpl; // forward declaration

/**
 * The type of a log entry ID.
 * The first valid entry is 0.
 * Appends to the log are assigned monotonically increasing IDs, but some
 * numbers may be skipped.
 */
typedef uint64_t EntryId;

/**
 * A reserved log ID.
 */
static const EntryId NO_ID = ~0UL;

/**
 * Encapsulates a blob of data in a single log entry.
 */
class Entry {
  public:
    /**
     * Constructor.
     * In this constructor, the entry ID defaults to NO_ID.
     * \param data
     *      Data that is owned by the caller. May be NULL if no data is to be
     *      associated with this entry.
     * \param length
     *      The number of bytes in data.
     * \param invalidates
     *      A list of entry IDs that this entry invalidates.
     */
    Entry(const void* data, uint32_t length,
          const std::vector<EntryId>& invalidates = std::vector<EntryId>());
    /**
     * Constructor.
     * In this constructor, the entry ID defaults to NO_ID and the data is not
     * set.
     * \param invalidates
     *      A list of entry IDs that this entry invalidates.
     */
    explicit Entry(const std::vector<EntryId>& invalidates);
    /// Move constructor.
    Entry(Entry&& other);
    /// Destructor.
    ~Entry();
    /// Move assignment.
    Entry& operator=(Entry&& other);
    /// Return the entry ID.
    EntryId getId() const;
    /// Return a list of entries that this entry invalidates.
    std::vector<EntryId> getInvalidates() const;
    /// Return the binary blob of data, or NULL if none is set.
    const void* getData() const;
    /// Return the number of bytes in data.
    uint32_t getLength() const;

  private:
    EntryId id;
    std::vector<EntryId> invalidates;
    std::unique_ptr<char[]> data;
    uint32_t length;
    // Entry is not copyable
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    friend class ClientImpl;
};


/**
 * This exception is thrown when operating on a log that has been deleted.
 * It almost always indicates a bug in the application.
 */
class LogDisappearedException : public std::exception {
};

/**
 * A handle to a replicated log.
 * You can get an instance of Log through Cluster::openLog.
 */
class Log {
  private:
    Log(std::shared_ptr<ClientImpl> clientImpl,
        const std::string& name,
        uint64_t logId);
  public:
    ~Log();

    /**
     * Append a new entry to the log.
     * \param entry
     *      The entry to append.
     * \param previousId
     *      Makes the operation conditional on this being the last ID in the
     *      log. Use NO_ID to unconditionally append.
     * \return
     *      The created entry ID, or NO_ID if the condition given by previousId
     *      failed.
     * \throw LogDisappearedException
     *      If this log no longer exists because someone deleted it.
     */
    EntryId append(const Entry& entry,
                   EntryId previousId = NO_ID);

    /**
     * Invalidate entries in the log.
     * This is just a convenient short-cut to appending an Entry, for appends
     * with no data.
     * \param invalidates
     *      A list of previous entries to be removed as part of this operation.
     * \param previousId
     *      Makes the operation conditional on this being the last ID in the
     *      log. Use NO_ID to unconditionally append.
     * \return
     *      The created entry ID, or NO_ID if the condition given by previousId
     *      failed. There's no need to invalidate this returned ID. It is the
     *      new head of the log, so it should be passed in future conditions as
     *      the previousId argument.
     * \throw LogDisappearedException
     *      If this log no longer exists because someone deleted it.
     */
    EntryId invalidate(const std::vector<EntryId>& invalidates,
                       EntryId previousId = NO_ID);

    /**
     * Read the entries starting at 'from' through head of the log.
     * \param from
     *      The entry at which to start reading.
     * \return
     *      The entries starting at and including 'from' through head of the
     *      log.
     * \throw LogDisappearedException
     *      If this log no longer exists because someone deleted it.
     */
    std::vector<Entry> read(EntryId from);

    /**
     * Return the ID for the head of the log.
     * \return
     *      The ID for the head of the log, or NO_ID if the log is empty.
     * \throw LogDisappearedException
     *      If this log no longer exists because someone deleted it.
     */
    EntryId getLastId();

  private:
    std::shared_ptr<ClientImpl> clientImpl;
    const std::string name;
    const uint64_t logId;
    friend class ClientImpl;
};

/**
 * A handle to the LogCabin cluster.
 */
class Cluster {
  public:
    /**
     * Constructor.
     * \param hosts
     *      A string describing the hosts in the cluster. This should be of the
     *      form host:port, where host is usually a DNS name that resolves to
     *      multiple IP addresses.
     */
    explicit Cluster(const std::string& hosts);
    ~Cluster();

    /**
     * Open the log by the given name.
     * If no log by that name exists, one will be created.
     */
    Log openLog(const std::string& logName);

    /**
     * Delete the log with the given name.
     * If no log by that name exists, this will do nothing.
     */
    void deleteLog(const std::string& logName);

    /**
     * Get a list of logs.
     * \return
     *      The name of each existing log in sorted order.
     */
    std::vector<std::string> listLogs();

  private:
    std::shared_ptr<ClientImpl> clientImpl;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENT_H */
