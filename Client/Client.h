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
#include <mutex>
#include <string>
#include <vector>

#ifndef LOGCABIN_CLIENT_CLIENT_H
#define LOGCABIN_CLIENT_CLIENT_H

namespace LogCabin {
namespace Client {

class ClientImplBase; // forward declaration

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
    friend class MockClientImpl;
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
    Log(std::shared_ptr<ClientImplBase> clientImpl,
        const std::string& name,
        uint64_t logId);
  public:
    ~Log();

    /**
     * Append a new entry to the log.
     * \param entry
     *      The entry to append.
     * \param expectedId
     *      Makes the operation conditional on this being the ID assigned to
     *      this log entry. For example, 0 would indicate the log must be empty
     *      for the operation to succeed. Use NO_ID to unconditionally append.
     * \return
     *      The created entry ID, or NO_ID if the condition given by expectedId
     *      failed.
     * \throw LogDisappearedException
     *      If this log no longer exists because someone deleted it.
     */
    EntryId append(const Entry& entry,
                   EntryId expectedId = NO_ID);

    /**
     * Invalidate entries in the log.
     * This is just a convenient short-cut to appending an Entry, for appends
     * with no data.
     * \param invalidates
     *      A list of previous entries to be removed as part of this operation.
     * \param expectedId
     *      Makes the operation conditional on this being the ID assigned to
     *      this log entry. For example, 0 would indicate the log must be empty
     *      for the operation to succeed. Use NO_ID to unconditionally append.
     * \return
     *      The created entry ID, or NO_ID if the condition given by expectedId
     *      failed. There's no need to invalidate this returned ID. It is the
     *      new head of the log, so one plus this should be passed in future
     *      conditions as the expectedId argument.
     * \throw LogDisappearedException
     *      If this log no longer exists because someone deleted it.
     */
    EntryId invalidate(const std::vector<EntryId>& invalidates,
                       EntryId expectedId = NO_ID);

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
    std::shared_ptr<ClientImplBase> clientImpl;
    const std::string name;
    const uint64_t logId;
    friend class ClientImpl;
    friend class MockClientImpl;
};

/**
 * A list of servers.
 * The first component is the server ID.
 * The second component is the network address of the server.
 * Used in Cluster::getConfiguration and Cluster::setConfiguration.
 */
typedef std::vector<std::pair<uint64_t, std::string>> Configuration;

/**
 * Returned by Cluster::setConfiguration.
 */
struct ConfigurationResult {
    ConfigurationResult();
    ~ConfigurationResult();
    enum Status {
        /**
         * The operation succeeded.
         */
        OK = 0,
        /**
         * The supplied 'oldId' is no longer current.
         * Call GetConfiguration, re-apply your changes, and try again.
         */
        CHANGED = 1,
        /**
         * The reconfiguration was aborted because some servers are
         * unavailable.
         */
        BAD = 2,
    } status;

    /**
     * If status is BAD, the servers that were unavailable to join the cluster.
     */
    Configuration badServers;
};

/**
 * Status codes returned by Tree operations.
 */
enum class Status {

    /**
     * The operation completed successfully.
     */
    OK = 0,

    /**
     * If an argument is malformed (for example, a path that does not start
     * with a slash).
     */
    INVALID_ARGUMENT = 1,

    /**
     * If a file or directory that is required for the operation does not
     * exist.
     */
    LOOKUP_ERROR = 2,

    /**
     * If a directory exists where a file is required or a file exists where
     * a directory is required.
     */
    TYPE_ERROR = 3,
};

/**
 * Print a status code to a stream.
 */
std::ostream&
operator<<(std::ostream& os, Status status);

/**
 * Returned by Tree operations; contain a status code and an error message.
 */
struct Result {
    /**
     * Default constructor. Sets status to OK and error to the empty string.
     */
    Result();
    /**
     * A code for whether an operation succeeded or why it did not. This is
     * meant to be used programmatically.
     */
    Status status;
    /**
     * If status is not OK, this is a human-readable message describing what
     * went wrong.
     */
    std::string error;
};

/**
 * Provides access to the hierarchical key-value store.
 * You can get an instance of Tree through Cluster::getTree() or by copying
 * an existing Tree.
 *
 * A Tree has a working directory from which all relative paths (those that do
 * not begin with a '/' are resolved). This allows different applications and
 * modules to conveniently access their own subtrees -- they can have their own
 * Tree instances and set their working directories accordingly.
 */
class Tree {
  private:
    /// Constructor.
    Tree(std::shared_ptr<ClientImplBase> clientImpl,
         const std::string& workingDirectory);
  public:
    /// Copy constructor.
    Tree(const Tree& other);
    /// Assignment operator.
    Tree& operator=(const Tree& other);

    /**
     * Set the working directory for this object. This directory will be
     * created if it does not exist.
     * \param workingDirectory
     *      The new working directory, which may be relative to the current
     *      working directory.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if workingDirectory is malformed.
     *       - TYPE_ERROR if workingDirectory parent of path is a file.
     *       - TYPE_ERROR if workingDirectory exists but is a file.
     *      If this returns an error, future operations on this tree using
     *      relative paths will fail until a valid working directory is set.
     */
    Result setWorkingDirectory(const std::string& workingDirectory);

    /**
     * Return the working directory for this object.
     * \return
     *      An absolute path that is the prefix for relative paths used with
     *      this Tree object.
     */
    std::string getWorkingDirectory() const;

    /**
     * Make sure a directory exists at the given path.
     * Create parent directories listed in path as necessary.
     * \param path
     *      The path where there should be a directory after this call.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is malformed.
     *       - TYPE_ERROR if a parent of path is a file.
     *       - TYPE_ERROR if path exists but is a file.
     */
    Result
    makeDirectory(const std::string& path);

    /**
     * List the contents of a directory.
     * \param path
     *      The directory whose direct children to list.
     * \param[out] children
     *      This will be replaced by a listing of the names of the directories
     *      and files that the directory at 'path' immediately contains. The
     *      names of directories in this listing will have a trailing slash.
     *      The order is first directories (sorted lexicographically), then
     *      files (sorted lexicographically).
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is malformed.
     *       - LOOKUP_ERROR if a parent of path does not exist.
     *       - LOOKUP_ERROR if path does not exist.
     *       - TYPE_ERROR if a parent of path is a file.
     *       - TYPE_ERROR if path exists but is a file.
     */
    Result
    listDirectory(const std::string& path, std::vector<std::string>& children);

    /**
     * Make sure a directory does not exist.
     * Also removes all direct and indirect children of the directory.
     *
     * If called with the root directory, this will remove all descendants but
     * not actually remove the root directory; it will still return status OK.
     *
     * \param path
     *      The path where there should not be a directory after this call.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is malformed.
     *       - TYPE_ERROR if a parent of path is a file.
     *       - TYPE_ERROR if path exists but is a file.
     */
    Result
    removeDirectory(const std::string& path);

    /**
     * Set the value of a file.
     * \param path
     *      The path where there should be a file with the given contents after
     *      this call.
     * \param contents
     *      The new value associated with the file.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is malformed.
     *       - INVALID_ARGUMENT if contents are too large to fit in a file.
     *       - LOOKUP_ERROR if a parent of path does not exist.
     *       - TYPE_ERROR if a parent of path is a file.
     *       - TYPE_ERROR if path exists but is a directory.
     */
    Result
    write(const std::string& path, const std::string& contents);

    /**
     * Get the value of a file.
     * \param path
     *      The path of the file whose contents to read.
     * \param contents
     *      The current value associated with the file.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is malformed.
     *       - LOOKUP_ERROR if a parent of path does not exist.
     *       - LOOKUP_ERROR if path does not exist.
     *       - TYPE_ERROR if a parent of path is a file.
     *       - TYPE_ERROR if path is a directory.
     */
    Result
    read(const std::string& path, std::string& contents);

    /**
     * Make sure a file does not exist.
     * \param path
     *      The path where there should not be a file after this call.
     * \return
     *      Status and error message. Possible errors are:
     *       - INVALID_ARGUMENT if path is malformed.
     *       - TYPE_ERROR if a parent of path is a file.
     *       - TYPE_ERROR if path exists but is a directory.
     */
    Result
    removeFile(const std::string& path);

  private:
    std::shared_ptr<ClientImplBase> clientImpl;
    mutable std::mutex mutex;
    std::string workingDirectory;
    friend class Cluster;
};

/**
 * A handle to the LogCabin cluster.
 */
class Cluster {
  public:

    /**
     * Defines a special type to use as an argument to the constructor that is
     * for testing purposes only.
     */
    enum ForTesting { FOR_TESTING };

    /**
     * Construct a Cluster object for testing purposes only. Instead of
     * connecting to a LogCabin cluster, it will keep all state locally in
     * memory.
     */
    explicit Cluster(ForTesting t);

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

    /**
     * Get the current, stable cluster configuration.
     * \return
     *      first: configurationId: Identifies the configuration.
     *             Pass this to setConfiguration later.
     *      second: The list of servers in the configuration.
     */
    std::pair<uint64_t, Configuration> getConfiguration();

    /**
     * Change the cluster's configuration.
     * \param oldId
     *      The ID of the cluster's current configuration.
     * \param newConfiguration
     *      The list of servers in the new configuration.
     */
    ConfigurationResult setConfiguration(
                                uint64_t oldId,
                                const Configuration& newConfiguration);

    /**
     * Return an object to access the hierarchical key-value store.
     * \return
     *      A Tree object with the working directory of '/'.
     */
    Tree getTree();

  private:
    std::shared_ptr<ClientImplBase> clientImpl;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_CLIENT_H */
