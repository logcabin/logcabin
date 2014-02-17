/* Copyright (c) 2012-2013 Stanford University
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

#include <mutex>
#include <string>
#include <vector>

#include "build/Server/SimpleFileLog.pb.h"
#include "Storage/FilesystemUtil.h"
#include "Server/MemoryLog.h"
#include "Server/RaftLog.h"

#ifndef LOGCABIN_SERVER_SIMPLEFILELOG_H
#define LOGCABIN_SERVER_SIMPLEFILELOG_H

namespace LogCabin {
namespace Server {

// forward declaration
class Globals;

namespace RaftConsensusInternal {

class SimpleFileLog : public Log {
    class Sync : public Log::Sync {
      public:
        explicit Sync(std::unique_ptr<Log::Sync> memSync);
        std::string wait();
        /**
         * Protects fds to avoid double-close.
         */
        std::mutex mutex;
        /**
         * Set of file descriptors that are fsynced and closed on wait().
         * If the bool is true, close it too.
         */
        std::vector<std::pair<int, bool>> fds;
    };

  public:
    typedef Protocol::Raft::Entry Entry;

    explicit SimpleFileLog(const Storage::FilesystemUtil::File& parentDir);
    ~SimpleFileLog();
    std::unique_ptr<Log::Sync>
        append(const std::vector<const Entry*>& entries);
    void truncatePrefix(uint64_t firstEntryId);
    void truncateSuffix(uint64_t lastEntryId);

    const Entry& getEntry(uint64_t) const;
    uint64_t getLogStartIndex() const;
    uint64_t getLastLogIndex() const;
    uint64_t getSizeBytes() const;


    void updateMetadata();

  protected:
    Storage::FilesystemUtil::File updateMetadataCallerSync();
    MemoryLog memoryLog;
    SimpleFileLogMetadata::Metadata metadata;
    Storage::FilesystemUtil::File dir;
    Storage::FilesystemUtil::File lostAndFound;

    std::string readMetadata(const std::string& filename,
                             SimpleFileLogMetadata::Metadata& metadata) const;
    std::vector<uint64_t> getEntryIds() const;
    Entry read(const std::string& entryPath) const;
};

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin

#endif /* LOGCABIN_SERVER_SIMPLEFILELOG_H */
