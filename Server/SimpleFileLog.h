/* Copyright (c) 2012 Stanford University
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

#include "build/Server/SimpleFileLog.pb.h"
#include "Storage/FilesystemUtil.h"
#include "Server/RaftLog.h"

#ifndef LOGCABIN_SERVER_SIMPLEFILELOG_H
#define LOGCABIN_SERVER_SIMPLEFILELOG_H

namespace LogCabin {
namespace Server {

// forward declaration
class Globals;

namespace RaftConsensusInternal {

class SimpleFileLog : public Log {
  public:

    typedef Protocol::Raft::Entry Entry;

    explicit SimpleFileLog(const std::string& path);
    ~SimpleFileLog();
    uint64_t append(const Entry& entry);
    void truncate(uint64_t lastEntryId);
    void updateMetadata();


  protected:
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
