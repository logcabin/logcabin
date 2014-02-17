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
    : startIndex(1)
    , entries()
{
}

MemoryLog::~MemoryLog()
{
}

std::unique_ptr<Log::Sync>
MemoryLog::append(const Entry& entry)
{
    uint64_t index = startIndex + entries.size();
    entries.push_back(entry);
    return std::unique_ptr<Sync>(
        new Sync(index, index));
}

const Log::Entry&
MemoryLog::getEntry(uint64_t index) const
{
    uint64_t offset = index - startIndex;
    return entries.at(offset);
}

uint64_t
MemoryLog::getLogStartIndex() const
{
    return startIndex;
}


uint64_t
MemoryLog::getLastLogIndex() const
{
    return startIndex + entries.size() - 1;
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
MemoryLog::truncatePrefix(uint64_t firstIndex)
{
    if (firstIndex > startIndex) {
        // Erase log entries in range [startIndex, firstIndex), so deque
        // offsets in range [0, firstIndex - startIndex). Be careful not to
        // erase past the end of the deque (STL doesn't check for this).
        entries.erase(entries.begin(),
                      entries.begin() + std::min(firstIndex - startIndex,
                                                 entries.size()));
        startIndex = firstIndex;
    }
}

void
MemoryLog::truncateSuffix(uint64_t lastIndex)
{
    if (lastIndex < startIndex)
        entries.clear();
    else if (lastIndex < startIndex - 1 + entries.size())
        entries.resize(lastIndex - startIndex + 1);
}

void
MemoryLog::updateMetadata()
{
}

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin
