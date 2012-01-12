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

#include "ClientImpl.h"

namespace DLog {
namespace Client {
namespace Internal {

ClientImpl::ClientImpl()
    : refCount()
    , errorCallback()
{
}

void
ClientImpl::registerErrorCallback(
                        std::unique_ptr<ErrorCallback> callback)
{
    this->errorCallback = std::move(callback);
}

Log
ClientImpl::openLog(const std::string& logName)
{
    return Log(ClientImplRef(*this),
               logName,
               0 /* TODO(ongaro): LogId */);
}

void
ClientImpl::deleteLog(const std::string& logName)
{
    // TODO(ongaro): implement
}

std::vector<std::string>
ClientImpl::listLogs()
{
    std::vector<std::string> logs;
    // TODO(ongaro): implement
    return logs;
}

std::vector<Entry>
ClientImpl::read(uint64_t logId, EntryId from)
{
    std::vector<Entry> entries;

    // TODO(ongaro): implement and remove this placeholder
    Entry e("foo", 4);
    e.id = from;
    entries.push_back(std::move(e));

    return entries;
}


EntryId
ClientImpl::append(uint64_t logId, Entry* data,
       const std::vector<EntryId>& invalidates,
       EntryId previousId)
{
    // TODO(ongaro): implement
    return NO_ID;
}

} // namespace DLog::Client::Internal
} // namespace DLog::Client
} // namespace DLog
