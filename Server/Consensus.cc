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

#include "Server/Consensus.h"
#include "Storage/SnapshotFile.h"

namespace LogCabin {
namespace Server {

__thread ThreadStat tstat;

Consensus::Entry::Entry()
    : entryId()
    , type(SKIP)
    , data()
    , snapshotReader()
    , request()
{
}

Consensus::Entry::Entry(Entry&& other)
    : entryId(other.entryId)
    , type(other.type)
    , data(std::move(other.data))
    , snapshotReader(std::move(other.snapshotReader))
    , request(std::move(other.request))
{
}

Consensus::Entry::~Entry()
{
}

Consensus::Consensus()
    : serverId(0)
    , serverAddress()
{
}

Consensus::~Consensus()
{
}

} // namespace LogCabin::Server
} // namespace LogCabin
