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

#include <algorithm>

#include "include/Common.h"
#include "Storage/LogEntry.h"
#include "Storage/MemoryModule.h"

namespace LogCabin {
namespace Storage {

////////// MemoryLog //////////

MemoryLog::MemoryLog(LogId logId)
    : Log(logId)
    , headId(NO_ENTRY_ID)
    , entries()
{
}

MemoryLog::~MemoryLog()
{
}

std::deque<const LogEntry*>
MemoryLog::readFrom(EntryId start) const
{
    std::deque<const LogEntry*> ret;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const LogEntry& entry = *it;
        if (entry.entryId < start)
            break;
        ret.push_front(&entry);
    }
    return ret;
}

EntryId
MemoryLog::append(LogEntry entry)
{
    EntryId newId = headId + 1;
    if (headId == NO_ENTRY_ID)
        newId = 0;
    headId = newId;
    entry.logId = logId;
    entry.entryId = newId;
    entries.push_back(std::move(entry));
    return newId;
}

////////// MemoryModule //////////

MemoryModule::MemoryModule()
    : logs()
{
}

MemoryModule::~MemoryModule()
{
    for (auto it = logs.begin(); it != logs.end(); ++it) {
        delete it->second;
    }
}

std::vector<LogId>
MemoryModule::getLogs()
{
    std::vector<LogId> ret = DLog::getKeys(logs);
    // This may help catch bugs in which the caller depends on
    // the ordering of this list, which is unspecified.
    std::random_shuffle(ret.begin(), ret.end());
    return ret;
}

MemoryLog*
MemoryModule::openLog(LogId logId)
{
    // Find logs that have been stashed away with putLog().
    auto it = logs.find(logId);
    if (it != logs.end()) {
        MemoryLog* log = it->second;
        logs.erase(it);
        return log;
    }

    return new MemoryLog(logId);
}

void
MemoryModule::deleteLog(LogId logId)
{
    auto it = logs.find(logId);
    if (it != logs.end()) {
        delete it->second;
        logs.erase(it);
    }
}

void
MemoryModule::putLog(MemoryLog* log)
{
    bool inserted = logs.insert({log->logId, log}).second;
    assert(inserted);
}

} // namespace LogCabin::Storage
} // namespace LogCabin
