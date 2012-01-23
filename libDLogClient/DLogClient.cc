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

#include <string.h>

#include "ClientImpl.h"
#include "DLogClient.h"
#include "Ref.h"

namespace DLog {
namespace Client {

namespace Internal {

// class ClientImplRef

ClientImplRef::ClientImplRef(ClientImpl& clientImpl)
    : clientImpl(&clientImpl)
{
    RefHelper<ClientImpl>::incRefCount(&clientImpl);
}

ClientImplRef::ClientImplRef(const ClientImplRef& other)
    : clientImpl(other.clientImpl)
{
    RefHelper<ClientImpl>::incRefCount(clientImpl);
}

ClientImplRef::~ClientImplRef()
{
    RefHelper<ClientImpl>::decRefCountAndDestroy(clientImpl);
}

ClientImplRef&
ClientImplRef::operator=(const ClientImplRef& other)
{
    RefHelper<ClientImpl>::incRefCount(other.clientImpl);
    RefHelper<ClientImpl>::decRefCountAndDestroy(clientImpl);
    clientImpl = other.clientImpl;
    return *this;
}

ClientImpl&
ClientImplRef::operator*() const
{
    return *clientImpl;
}

ClientImpl*
ClientImplRef::operator->() const
{
    return clientImpl;
}

} // namespace DLog::Client::Internal

// class Entry

Entry::Entry(const void* data, uint32_t length,
             const std::vector<EntryId>& invalidates)
    : id(NO_ID)
    , invalidates(invalidates)
    , data(new char[length])
    , length(length)
{
    memcpy(this->data.get(), data, length);
}

Entry::Entry(const std::vector<EntryId>& invalidates)
    : id(NO_ID)
    , invalidates(invalidates)
    , data()
    , length(0)
{
}

Entry::Entry(Entry&& other)
    : id(other.id)
    , invalidates(std::move(other.invalidates))
    , data(other.data.release())
    , length(other.length)
{
}

Entry::~Entry()
{
}

Entry&
Entry::operator=(Entry&& other)
{
    id = other.id;
    data = std::move(other.data);
    length = other.length;
    return *this;
}

EntryId
Entry::getId() const
{
    return id;
}

std::vector<EntryId>
Entry::getInvalidates() const
{
    return invalidates;
}

const void*
Entry::getData() const
{
    return data.get();
}

uint32_t
Entry::getLength() const
{
    return length;
}

// class Log

Log::Log(Internal::ClientImplRef clientImpl,
         const std::string& name,
         uint64_t logId)
    : clientImpl(clientImpl)
    , name(name)
    , logId(logId)
{
}

Log::~Log()
{
}

EntryId
Log::append(const Entry& entry, EntryId previousId)
{
    return clientImpl->append(logId, entry, previousId);
}

EntryId
Log::invalidate(const std::vector<EntryId>& invalidates,
                EntryId previousId)
{
    Entry entry(invalidates);
    return clientImpl->append(logId, entry, previousId);
}

std::vector<Entry>
Log::read(EntryId from)
{
    return clientImpl->read(logId, from);
}

EntryId
Log::getLastId()
{
    return clientImpl->getLastId(logId);
}

// class Cluster

Cluster::Cluster(const std::string& hosts)
    : clientImpl(*make<Internal::ClientImpl>())
{
}

Cluster::~Cluster()
{
}

void
Cluster::registerErrorCallback(std::unique_ptr<ErrorCallback> callback)
{
    clientImpl->registerErrorCallback(std::move(callback));
}

Log
Cluster::openLog(const std::string& logName)
{
    return clientImpl->openLog(logName);
}

void
Cluster::deleteLog(const std::string& logName)
{
    clientImpl->deleteLog(logName);
}

std::vector<std::string>
Cluster::listLogs()
{
    return clientImpl->listLogs();
}

} // namespace DLog::Client
} // namespace DLog
