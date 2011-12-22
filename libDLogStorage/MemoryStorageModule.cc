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

#include <algorithm>

#include "MemoryStorageModule.h"

namespace DLog {

namespace Storage {

// class MemoryLog

MemoryLog::MemoryLog(LogId logId)
    : Log(logId)
    , headId(NO_ENTRY_ID)
    , entries() {
}

std::deque<LogEntry>
MemoryLog::readFrom(EntryId start)
{
    std::deque<LogEntry> ret;
    for (auto it = entries.rbegin();
         it != entries.rend();
         ++it) {
        LogEntry& entry = *it;
        if (entry.entryId < start)
            break;
        ret.push_front(entry);
    }
    return ret;
}

void
MemoryLog::append(LogEntry& entry,
                  Ref<AppendCallback> appendCompletion)
{
    EntryId newId = headId + 1;
    if (headId == NO_ENTRY_ID)
        newId = 0;
    headId = newId;
    entry.logId = getLogId();
    entry.entryId = newId;
    entries.push_back(entry);
    appendCompletion->appended(entries.back());
}

// class MemoryStorageModule

MemoryStorageModule::MemoryStorageModule()
    : logs()
{
}

std::vector<LogId>
MemoryStorageModule::getLogs()
{
    std::vector<LogId> ret = getKeys(logs);
    // This may help catch bugs in which the caller depends on
    // the ordering of this list, which is unspecified.
    std::random_shuffle(ret.begin(), ret.end());
    return ret;
}

Ref<Log>
MemoryStorageModule::openLog(LogId logId)
{
    // This is not strictly necessary but makes testing easier.
    auto it = logs.find(logId);
    if (it != logs.end())
        return it->second;

    Ref<Log> newLog = make<MemoryLog>(logId);
    logs.insert({logId, newLog});
    return newLog;
}

void
MemoryStorageModule::deleteLog(LogId logId,
                               Ref<DeleteCallback> deleteCompletion)
{
    logs.erase(logId);
    deleteCompletion->deleted(logId);
}

} // namespace DLog::Storage
} // namespace DLog
