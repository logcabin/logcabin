/* Copyright (c) 2012-2014 Stanford University
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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "build/Protocol/Raft.pb.h"
#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "RPC/Buffer.h"
#include "RPC/ProtoBuf.h"
#include "Server/MemoryLog.h"

namespace LogCabin {
namespace Server {
namespace RaftConsensusInternal {

////////// MemoryLog //////////

MemoryLog::MemoryLog()
    : startId(1)
    , entries()
{
}

MemoryLog::~MemoryLog()
{
}

std::unique_ptr<Log::Sync>
MemoryLog::append(const Entry& entry)
{
    uint64_t entryId = startId + entries.size();
    entries.push_back(entry);
    return std::unique_ptr<Sync>(
        new Sync(entryId, entryId));
}

const Log::Entry&
MemoryLog::getEntry(uint64_t entryId) const
{
    uint64_t index = entryId - startId;
    return entries.at(index);
}

uint64_t
MemoryLog::getLogStartIndex() const
{
    return startId;
}


uint64_t
MemoryLog::getLastLogIndex() const
{
    return startId + entries.size() - 1;
}

uint64_t
MemoryLog::getSizeBytes() const
{
    // TODO(ongaro): keep this pre-calculated for efficiency
    uint64_t size = 0;
    for (auto it = entries.begin(); it < entries.end(); ++it)
        size += it->ByteSize();
    return size;
}

void
MemoryLog::truncatePrefix(uint64_t firstEntryId)
{
    if (firstEntryId > startId) {
        // Erase log IDs in range [startId, firstEntryId), so deque indexes in
        // range [0, firstEntryId - startId). Be careful not to erase past the
        // end of the deque (STL doesn't check for this).
        entries.erase(entries.begin(),
                      entries.begin() + std::min(firstEntryId - startId,
                                                 entries.size()));
        startId = firstEntryId;
    }
}

void
MemoryLog::truncateSuffix(uint64_t lastEntryId)
{
    if (lastEntryId < startId)
        entries.clear();
    else if (lastEntryId < startId - 1 + entries.size())
        entries.resize(lastEntryId - startId + 1);
}

void
MemoryLog::updateMetadata()
{
}

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin
