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
#include "Server/RaftLog.h"

namespace LogCabin {
namespace Server {
namespace RaftConsensusInternal {

////////// Log //////////

Log::Log()
    : metadata()
    , entries()
{
}

Log::~Log()
{
}

uint64_t
Log::append(const Entry& entry)
{
    entries.push_back(entry);
    uint64_t entryId = entries.size();
    return entryId;
}

uint64_t
Log::getBeginLastTermId() const
{
    uint64_t entryId = getLastLogId();
    uint64_t lastLogTerm = getTerm(entryId);
    if (entryId == 0)
        return 0;
    while (true) {
        --entryId;
        if (getTerm(entryId) != lastLogTerm)
            return entryId + 1;
    }
}

const Log::Entry&
Log::getEntry(uint64_t entryId) const
{
    uint64_t index = entryId - 1;
    return entries.at(index);
}

uint64_t
Log::getLastLogId() const
{
    return entries.size();
}


uint64_t
Log::getTerm(uint64_t entryId) const
{
    uint64_t index = entryId - 1; // may roll over to ~0UL
    if (index >= entries.size())
        return 0;
    return entries.at(index).term();
}

void
Log::truncate(uint64_t lastEntryId)
{
    if (lastEntryId < entries.size())
        entries.resize(lastEntryId);
}

void
Log::updateMetadata()
{
}

} // namespace LogCabin::Server::RaftConsensusInternal
} // namespace LogCabin::Server
} // namespace LogCabin
