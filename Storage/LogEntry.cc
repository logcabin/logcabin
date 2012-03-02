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

#include <sstream>

#include "include/Common.h"
#include "Storage/LogEntry.h"

namespace LogCabin {
namespace Storage {

LogEntry::LogEntry(TimeStamp createTime,
                   RPC::Buffer data,
                   std::vector<EntryId> invalidations)
    : logId(NO_LOG_ID)
    , entryId(NO_ENTRY_ID)
    , createTime(createTime)
    , invalidations(std::move(invalidations))
    , hasData(true)
    , data(std::move(data))
{
}

LogEntry::LogEntry(TimeStamp createTime,
                   std::vector<EntryId> invalidations)
    : logId(NO_LOG_ID)
    , entryId(NO_ENTRY_ID)
    , createTime(createTime)
    , invalidations(std::move(invalidations))
    , hasData(false)
    , data()
{
}

LogEntry::LogEntry(LogEntry&& other)
    : logId(std::move(other.logId))
    , entryId(std::move(other.entryId))
    , createTime(std::move(other.createTime))
    , invalidations(std::move(other.invalidations))
    , hasData(std::move(other.hasData))
    , data(std::move(other.data))
{
}

LogEntry&
LogEntry::operator=(LogEntry&& other)
{
    logId = std::move(other.logId);
    entryId = std::move(other.entryId);
    createTime = std::move(other.createTime);
    invalidations = std::move(other.invalidations);
    hasData = std::move(other.hasData);
    data = std::move(other.data);
    return *this;
}

std::string
LogEntry::toString() const
{
    std::ostringstream s;
    s << "(" << logId << ", " << entryId << ") ";
    if (!hasData) {
        s << "NODATA";
    } else {
        if (DLog::isPrintable(data.getData(), data.getLength()))
            s << "'" << static_cast<const char*>(data.getData()) << "'";
        else
            s << "BINARY";
    }
    if (!invalidations.empty()) {
        s << " [inv ";
        for (uint32_t i = 0; i != invalidations.size(); ++i) {
            s << invalidations.at(i);
            if (i < invalidations.size() - 1)
                s << ", ";
        }
        s << "]";
    }
    return s.str();
}

} // namespace LogCabin::Storage
} // namespace LogCabin
