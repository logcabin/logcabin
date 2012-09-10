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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "build/Protocol/Client.pb.h"

#ifndef LOGCABIN_SERVER_STATEMACHINE_H
#define LOGCABIN_SERVER_STATEMACHINE_H

namespace LogCabin {
namespace Server {

// forward declaration
class Consensus;

class StateMachine {
  public:
    explicit StateMachine(std::shared_ptr<Consensus> consensus);
    ~StateMachine();

    Protocol::Client::CommandResponse getResponse(uint64_t id) const;

    void wait(uint64_t entryId) const;

    void listLogs(const Protocol::Client::ListLogs::Request& request,
                  Protocol::Client::ListLogs::Response& response) const;

    void read(const Protocol::Client::Read::Request& request,
                  Protocol::Client::Read::Response& response) const;

    void getLastId(const Protocol::Client::GetLastId::Request& request,
                   Protocol::Client::GetLastId::Response& response) const;

  private:
    void threadMain();

    typedef Protocol::Client::Read::Response::OK::Entry Entry;
    typedef std::vector<Entry> Log;

    void advance(uint64_t entryId, const std::string& data);

    void openLog(const Protocol::Client::OpenLog::Request& request,
                 Protocol::Client::OpenLog::Response& response);
    void deleteLog(const Protocol::Client::DeleteLog::Request& request,
                   Protocol::Client::DeleteLog::Response& response);
    void append(const Protocol::Client::Append::Request& request,
                Protocol::Client::Append::Response& response);

    std::shared_ptr<Consensus> consensus;
    mutable std::mutex mutex;
    mutable std::condition_variable cond;
    std::thread thread;
    uint64_t lastEntryId; // only written to by thread
    std::unordered_map<uint64_t, Protocol::Client::CommandResponse> responses;

    /**
     * Look up a log by ID or throw LogDisappearedException.
     * Must be called holding #mutex.
     */
    std::vector<Entry>& getLog(uint64_t logId);

    uint64_t nextLogId;
    std::map<std::string, uint64_t> logNames;
    // This shared_ptr just exists to make std::vector<Entry> copyable.
    // This is a work-around for gcc 4.4, which can't handle move-only objects
    // in maps.
    std::unordered_map<uint64_t, std::shared_ptr<Log>> logs;
};

} // namespace LogCabin::Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_STATEMACHINE_H
