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

#include "Client/Client.h"
#include "Client/ClientImplBase.h"
#include "Client/ClientImpl.h"
#include "Client/MockClientImpl.h"
#include "Core/StringUtil.h"

namespace LogCabin {
namespace Client {

////////// Entry //////////

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

////////// Log //////////

Log::Log(std::shared_ptr<ClientImplBase> clientImpl,
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
Log::append(const Entry& entry, EntryId expectedId)
{
    return clientImpl->append(logId, entry, expectedId);
}

EntryId
Log::invalidate(const std::vector<EntryId>& invalidates,
                EntryId expectedId)
{
    Entry entry(invalidates);
    return clientImpl->append(logId, entry, expectedId);
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

////////// ConfigurationResult //////////

ConfigurationResult::ConfigurationResult()
    : status(OK)
    , badServers()
{
}

ConfigurationResult::~ConfigurationResult()
{
}

////////// enum Status //////////

std::ostream&
operator<<(std::ostream& os, Status status)
{
    switch (status) {
        case Status::OK:
            os << "Status::OK";
            break;
        case Status::INVALID_ARGUMENT:
            os << "Status::INVALID_ARGUMENT";
            break;
        case Status::LOOKUP_ERROR:
            os << "Status::LOOKUP_ERROR";
            break;
        case Status::TYPE_ERROR:
            os << "Status::TYPE_ERROR";
            break;
    }
    return os;
}

////////// struct Result //////////

Result::Result()
    : status(Status::OK)
    , error()
{
}

////////// Tree //////////

Tree::Tree(std::shared_ptr<ClientImplBase> clientImpl,
           const std::string& workingDirectory)
    : clientImpl(clientImpl)
    , mutex()
    , workingDirectory(workingDirectory)
{
}

Tree::Tree(const Tree& other)
    : clientImpl(other.clientImpl)
    , mutex()
    , workingDirectory(other.getWorkingDirectory())
{
}

Tree&
Tree::operator=(const Tree& other)
{
    clientImpl = other.clientImpl;
    std::unique_lock<std::mutex> lockGuard(mutex);
    workingDirectory = other.getWorkingDirectory();
    return *this;
}

Result
Tree::setWorkingDirectory(const std::string& newWorkingDirectory)
{
    // This method sets the working directory regardless of whether it
    // succeeds -- that way if it doesn't, future relative paths on this Tree
    // will result in errors instead of operating on the prior working
    // directory.

    std::unique_lock<std::mutex> lockGuard(mutex);
    std::string realPath;
    Result result = clientImpl->canonicalize(newWorkingDirectory,
                                             workingDirectory,
                                             realPath);
    if (result.status != Status::OK) {
        workingDirectory = Core::StringUtil::format(
                    "invalid from prior call to setWorkingDirectory('%s') "
                    "relative to '%s'",
                    newWorkingDirectory.c_str(), workingDirectory.c_str());
        return result;
    }
    workingDirectory = realPath;
    return clientImpl->makeDirectory(realPath, "");
}

std::string
Tree::getWorkingDirectory() const
{
    std::string ret;
    {
        std::unique_lock<std::mutex> lockGuard(mutex);
        ret = workingDirectory;
    }
    return ret;
}

Result
Tree::makeDirectory(const std::string& path)
{
    return clientImpl->makeDirectory(path, getWorkingDirectory());
}

Result
Tree::listDirectory(const std::string& path,
                       std::vector<std::string>& children)
{
    return clientImpl->listDirectory(path, getWorkingDirectory(), children);
}

Result
Tree::removeDirectory(const std::string& path)
{
    return clientImpl->removeDirectory(path, getWorkingDirectory());
}

Result
Tree::write(const std::string& path, const std::string& contents)
{
    return clientImpl->write(path, getWorkingDirectory(), contents);
}

Result
Tree::read(const std::string& path, std::string& contents)
{
    return clientImpl->read(path, getWorkingDirectory(), contents);
}

Result
Tree::removeFile(const std::string& path)
{
    return clientImpl->removeFile(path, getWorkingDirectory());
}


////////// Cluster //////////

Cluster::Cluster(ForTesting t)
    : clientImpl(std::make_shared<MockClientImpl>())
{
    clientImpl->init(clientImpl, "-MOCK-");
}

Cluster::Cluster(const std::string& hosts)
    : clientImpl(std::make_shared<ClientImpl>())
{
#if DEBUG // for testing purposes only
    if (hosts == "-MOCK-SKIP-INIT-")
        return;
#endif
    clientImpl->init(clientImpl, hosts);
}

Cluster::~Cluster()
{
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

std::pair<uint64_t, Configuration>
Cluster::getConfiguration()
{
    return clientImpl->getConfiguration();
}

ConfigurationResult
Cluster::setConfiguration(uint64_t oldId,
                          const Configuration& newConfiguration)
{
    return clientImpl->setConfiguration(oldId, newConfiguration);
}

Tree
Cluster::getTree()
{
    return Tree(clientImpl, "/");
}

} // namespace LogCabin::Client
} // namespace LogCabin
