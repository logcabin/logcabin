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

#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>
#include "Client/Client.h"
#include "Client/ClientImplBase.h"

#ifndef LOGCABIN_CLIENT_MOCKCLIENTIMPL_H
#define LOGCABIN_CLIENT_MOCKCLIENTIMPL_H

namespace LogCabin {
namespace Client {

/**
 * The implementation of the client library.
 * This is wrapped by Client::Cluster and Client::Log for usability.
 */
class MockClientImpl : public ClientImplBase {
  public:
    /// Constructor.
    MockClientImpl();
    /// Destructor.
    ~MockClientImpl();

    // Implementations of ClientImplBase methods
    Log openLog(const std::string& logName);
    void deleteLog(const std::string& logName);
    std::vector<std::string> listLogs();
    EntryId append(uint64_t logId, const Entry& entry, EntryId expectedId);
    std::vector<Entry> read(uint64_t logId, EntryId from);
    EntryId getLastId(uint64_t logId);
    std::pair<uint64_t, Configuration> getConfiguration();
    ConfigurationResult setConfiguration(
                uint64_t oldId,
                const Configuration& newConfiguration);

  private:

    /**
     * Look up a log by ID or throw LogDisappearedException.
     * Must be called holding #mutex.
     */
    std::vector<Entry>& getLog(uint64_t logId);

    std::mutex mutex;
    uint64_t nextLogId;
    std::map<std::string, uint64_t> logNames;
    // This shared_ptr just exists to make std::vector<Entry> copyable.
    // This is a work-around for gcc 4.4, which can't handle move-only objects
    // in maps.
    std::unordered_map<uint64_t, std::shared_ptr<std::vector<Entry>>> logs;

    // MockClientImpl is not copyable
    MockClientImpl(const MockClientImpl&) = delete;
    MockClientImpl& operator=(const MockClientImpl&) = delete;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_MOCKCLIENTIMPL_H */
