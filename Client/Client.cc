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

#include "include/LogCabin/Client.h"
#include "Client/ClientImplBase.h"
#include "Client/ClientImpl.h"
#include "Client/MockClientImpl.h"
#include "Core/StringUtil.h"

namespace LogCabin {
namespace Client {

namespace {
void throwException(const Result& result)
{
    switch (result.status) {
        case Status::OK:
            return;
        case Status::INVALID_ARGUMENT:
            throw InvalidArgumentException(result.error);
        case Status::LOOKUP_ERROR:
            throw LookupException(result.error);
        case Status::TYPE_ERROR:
            throw TypeException(result.error);
        case Status::CONDITION_NOT_MET:
            throw ConditionNotMetException(result.error);
    }
}
} // anonymous namespace


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
        case Status::CONDITION_NOT_MET:
            os << "Status::CONDITION_NOT_MET";
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

////////// class Exception //////////

Exception::Exception(const std::string& error)
    : std::runtime_error(error)
{
}

InvalidArgumentException::InvalidArgumentException(const std::string& error)
    : Exception(error)
{
}

LookupException::LookupException(const std::string& error)
    : Exception(error)
{
}

TypeException::TypeException(const std::string& error)
    : Exception(error)
{
}

ConditionNotMetException::ConditionNotMetException(const std::string& error)
    : Exception(error)
{
}

////////// TreeDetails //////////

/**
 * Implementation-specific members of Client::Tree.
 */
class TreeDetails {
  public:
    TreeDetails(std::shared_ptr<ClientImplBase> clientImpl,
                const std::string& workingDirectory)
        : clientImpl(clientImpl)
        , workingDirectory(workingDirectory)
        , condition()
    {
    }
    /**
     * Client implementation.
     */
    std::shared_ptr<ClientImplBase> clientImpl;
    /**
     * The current working directory for the Tree (an absolute path).
     */
    std::string workingDirectory;
    /**
     * If set, specifies a predicate that must hold for operations to take
     * effect.
     */
    Condition condition;
};


////////// Tree //////////

Tree::Tree(std::shared_ptr<ClientImplBase> clientImpl,
           const std::string& workingDirectory)
    : mutex()
    , treeDetails(new TreeDetails(clientImpl, workingDirectory))
{
}

Tree::Tree(const Tree& other)
    : mutex()
    , treeDetails(other.getTreeDetails())
{
}

Tree&
Tree::operator=(const Tree& other)
{
    // Hold one lock at a time to avoid deadlock and handle self-assignment.
    std::shared_ptr<const TreeDetails> otherTreeDetails =
                                            other.getTreeDetails();
    std::unique_lock<std::mutex> lockGuard(mutex);
    treeDetails = otherTreeDetails;
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
    Result result = treeDetails->clientImpl->canonicalize(
                                newWorkingDirectory,
                                treeDetails->workingDirectory,
                                realPath);
    std::shared_ptr<TreeDetails> newTreeDetails(new TreeDetails(*treeDetails));
    if (result.status != Status::OK) {
        newTreeDetails->workingDirectory = Core::StringUtil::format(
                    "invalid from prior call to setWorkingDirectory('%s') "
                    "relative to '%s'",
                    newWorkingDirectory.c_str(),
                    treeDetails->workingDirectory.c_str());
        treeDetails = newTreeDetails;
        return result;
    }
    newTreeDetails->workingDirectory = realPath;
    treeDetails = newTreeDetails;
    return treeDetails->clientImpl->makeDirectory(realPath, "",
                                                  treeDetails->condition);
}

void
Tree::setWorkingDirectoryEx(const std::string& workingDirectory)
{
    throwException(setWorkingDirectory(workingDirectory));
}

std::string
Tree::getWorkingDirectory() const
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->workingDirectory;
}

Result
Tree::setCondition(const std::string& path, const std::string& value)
{
    // This method sets the condition regardless of whether it succeeds -- that
    // way if it doesn't, future calls on this Tree will result in errors
    // instead of operating on the prior condition.

    std::unique_lock<std::mutex> lockGuard(mutex);
    std::string realPath;
    Result result = treeDetails->clientImpl->canonicalize(
                                path,
                                treeDetails->workingDirectory,
                                realPath);
    std::shared_ptr<TreeDetails> newTreeDetails(new TreeDetails(*treeDetails));
    if (result.status != Status::OK) {
        newTreeDetails->condition = {
            Core::StringUtil::format(
                    "invalid from prior call to setCondition('%s') "
                    "relative to '%s'",
                    path.c_str(),
                    treeDetails->workingDirectory.c_str()),
            value,
        };
        treeDetails = newTreeDetails;
        return result;
    }
    newTreeDetails->condition = {path, value};
    treeDetails = newTreeDetails;
    return Result();
}

void
Tree::setConditionEx(const std::string& path, const std::string& value)
{
    throwException(setCondition(path, value));
}

std::pair<std::string, std::string>
Tree::getCondition() const
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->condition;
}

Result
Tree::makeDirectory(const std::string& path)
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->clientImpl->makeDirectory(
                                        path,
                                        treeDetails->workingDirectory,
                                        treeDetails->condition);
}

void
Tree::makeDirectoryEx(const std::string& path)
{
    throwException(makeDirectory(path));
}

Result
Tree::listDirectory(const std::string& path,
                    std::vector<std::string>& children) const
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->clientImpl->listDirectory(
                                        path,
                                        treeDetails->workingDirectory,
                                        treeDetails->condition,
                                        children);
}

std::vector<std::string>
Tree::listDirectoryEx(const std::string& path) const
{
    std::vector<std::string> children;
    throwException(listDirectory(path, children));
    return children;
}

Result
Tree::removeDirectory(const std::string& path)
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->clientImpl->removeDirectory(
                                        path,
                                        treeDetails->workingDirectory,
                                        treeDetails->condition);
}

void
Tree::removeDirectoryEx(const std::string& path)
{
    throwException(removeDirectory(path));
}

Result
Tree::write(const std::string& path, const std::string& contents)
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->clientImpl->write(path,
                                          treeDetails->workingDirectory,
                                          contents,
                                          treeDetails->condition);
}

void
Tree::writeEx(const std::string& path, const std::string& contents)
{
    throwException(write(path, contents));
}

Result
Tree::read(const std::string& path, std::string& contents) const
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->clientImpl->read(path,
                                         treeDetails->workingDirectory,
                                         treeDetails->condition,
                                         contents);
}

std::string
Tree::readEx(const std::string& path) const
{
    std::string contents;
    throwException(read(path, contents));
    return contents;
}

Result
Tree::removeFile(const std::string& path)
{
    std::shared_ptr<const TreeDetails> treeDetails = getTreeDetails();
    return treeDetails->clientImpl->removeFile(path,
                                               treeDetails->workingDirectory,
                                               treeDetails->condition);
}

void
Tree::removeFileEx(const std::string& path)
{
    throwException(removeFile(path));
}

std::shared_ptr<const TreeDetails>
Tree::getTreeDetails() const
{
    std::shared_ptr<const TreeDetails> ret;
    std::unique_lock<std::mutex> lockGuard(mutex);
    ret = treeDetails;
    return ret;
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

std::pair<uint64_t, Configuration>
Cluster::getConfiguration() const
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
