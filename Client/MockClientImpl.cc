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

#include "Core/STLUtil.h"
#include "Client/MockClientImpl.h"

namespace LogCabin {
namespace Client {

MockClientImpl::MockClientImpl()
    : mutex()
    , nextLogId(0)
    , logNames()
    , logs()
{
}

MockClientImpl::~MockClientImpl()
{
}

Log
MockClientImpl::openLog(const std::string& logName)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto it = logNames.find(logName);
    uint64_t logId;
    if (it != logNames.end()) {
        logId = it->second;
    } else {
        logId = nextLogId;
        ++nextLogId;
        logNames.insert({logName, logId});
        logs.insert({logId, std::make_shared<std::vector<Entry>>()});
    }
    return Log(self.lock(), logName, logId);
}

void
MockClientImpl::deleteLog(const std::string& logName)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto it = logNames.find(logName);
    if (it == logNames.end())
        return;
    uint64_t logId = it->second;
    logNames.erase(it);
    logs.erase(logId);
}

std::vector<std::string>
MockClientImpl::listLogs()
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    return Core::STLUtil::getKeys(logNames);
}

EntryId
MockClientImpl::append(uint64_t logId, const Entry& entry, EntryId expectedId)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    std::vector<Entry>& log = getLog(logId);
    EntryId newId = log.size();
    if (expectedId != NO_ID && expectedId != newId)
        return NO_ID;
    log.emplace_back(entry.data.get(), entry.length, entry.invalidates);
    log.back().id = newId;
    return newId;
}

std::vector<Entry>
MockClientImpl::read(uint64_t logId, EntryId from)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    std::vector<Entry>& log = getLog(logId);
    std::vector<Entry> ret;
    for (auto it = log.begin(); it != log.end(); ++it) {
        if (it->id < from)
            continue;
        ret.emplace_back(it->data.get(), it->length, it->invalidates);
        ret.back().id = it->id;
    }
    return ret;
}

EntryId
MockClientImpl::getLastId(uint64_t logId)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    std::vector<Entry>& log = getLog(logId);
    if (log.empty())
        return NO_ID;
    return log.back().id;
}

std::pair<uint64_t, Configuration>
MockClientImpl::getConfiguration()
{
    return {0, {}};
}

ConfigurationResult
MockClientImpl::setConfiguration(uint64_t oldId,
                                 const Configuration& newConfiguration)
{
    ConfigurationResult result;
    result.status = ConfigurationResult::BAD;
    result.badServers = newConfiguration;
    return result;
}

std::vector<Entry>&
MockClientImpl::getLog(uint64_t logId)
{
    auto it = logs.find(logId);
    if (it == logs.end())
        throw LogDisappearedException();
    return *it->second;
}

} // namespace LogCabin::Client
} // namespace LogCabin
