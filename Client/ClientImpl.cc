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

#include "Core/Debug.h"
#include "Client/ClientImpl.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "RPC/Address.h"

namespace LogCabin {
namespace Client {

namespace {
/**
 * The oldest RPC protocol version that this client library supports.
 */
const uint32_t MIN_RPC_PROTOCOL_VERSION = 1;

/**
 * The newest RPC protocol version that this client library supports.
 */
const uint32_t MAX_RPC_PROTOCOL_VERSION = 1;
}

using Protocol::Client::OpCode;


////////// class ClientImpl::ExactlyOnceRPCHelper //////////

ClientImpl::ExactlyOnceRPCHelper::ExactlyOnceRPCHelper(ClientImpl* client)
    : client(client)
    , mutex()
    , outstandingRPCNumbers()
    , clientId(0)
    , nextRPCNumber(1)
    , keepAliveCV()
    , exiting(false)
    , lastKeepAliveStart(TimePoint::min())
      // TODO(ongaro): set dynamically based on cluster configuration
    , keepAliveIntervalMs(60 * 1000)
    , keepAliveThread()
{
}

ClientImpl::ExactlyOnceRPCHelper::~ExactlyOnceRPCHelper()
{
}

void
ClientImpl::ExactlyOnceRPCHelper::exit()
{
    {
        std::unique_lock<std::mutex> lockGuard(mutex);
        exiting = true;
        keepAliveCV.notify_all();
        // TODO(ongaro): would be better if we could cancel keep-alive calls
    }
    if (keepAliveThread.joinable())
        keepAliveThread.join();
}

Protocol::Client::ExactlyOnceRPCInfo
ClientImpl::ExactlyOnceRPCHelper::getRPCInfo()
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    Protocol::Client::ExactlyOnceRPCInfo rpcInfo;
    if (client == NULL) {
        // Filling in rpcInfo is disabled for some unit tests, since it's
        // easier if they treat rpcInfo opaquely.
        return rpcInfo;
    }
    if (clientId == 0) {
        lastKeepAliveStart = Clock::now();
        Protocol::Client::OpenSession::Request request;
        Protocol::Client::OpenSession::Response response;
        client->leaderRPC->call(OpCode::OPEN_SESSION, request, response);
        clientId = response.client_id();
        assert(clientId > 0);
        keepAliveThread = std::thread(
            &ClientImpl::ExactlyOnceRPCHelper::keepAliveThreadMain,
            this);
    }

    lastKeepAliveStart = Clock::now();
    keepAliveCV.notify_all();
    rpcInfo.set_client_id(clientId);
    uint64_t rpcNumber = nextRPCNumber;
    ++nextRPCNumber;
    rpcInfo.set_rpc_number(rpcNumber);
    outstandingRPCNumbers.insert(rpcNumber);
    rpcInfo.set_first_outstanding_rpc(*outstandingRPCNumbers.begin());
    return rpcInfo;
}

void
ClientImpl::ExactlyOnceRPCHelper::doneWithRPC(
                    const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    outstandingRPCNumbers.erase(rpcInfo.rpc_number());
}

void
ClientImpl::ExactlyOnceRPCHelper::keepAliveThreadMain()
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    while (true) {
        if (exiting)
            return;
        TimePoint nextKeepAlive;
        if (keepAliveIntervalMs > 0) {
            nextKeepAlive = (lastKeepAliveStart +
                             std::chrono::milliseconds(keepAliveIntervalMs));
        } else {
            nextKeepAlive = TimePoint::max();
        }
        if (Clock::now() > nextKeepAlive) {
            // release lock to avoid deadlock
            Core::MutexUnlock<std::mutex> unlockGuard(lockGuard);
            client->keepAlive(); // will set nextKeepAlive
            continue;
        }
        keepAliveCV.wait_until(lockGuard, nextKeepAlive);
    }
}

////////// class ClientImpl //////////

ClientImpl::ClientImpl()
    : leaderRPC()             // set in init()
    , rpcProtocolVersion(~0U) // set in init()
    , exactlyOnceRPCHelper(this)
{
}

ClientImpl::~ClientImpl()
{
    exactlyOnceRPCHelper.exit();
}

void
ClientImpl::initDerived()
{
    if (!leaderRPC) // sometimes set in unit tests
        leaderRPC.reset(new LeaderRPC(RPC::Address(hosts, 0)));
    rpcProtocolVersion = negotiateRPCVersion();
}

std::pair<uint64_t, Configuration>
ClientImpl::getConfiguration()
{
    Protocol::Client::GetConfiguration::Request request;
    Protocol::Client::GetConfiguration::Response response;
    leaderRPC->call(OpCode::GET_CONFIGURATION, request, response);
    Configuration configuration;
    for (auto it = response.servers().begin();
         it != response.servers().end();
         ++it) {
        configuration.emplace_back(it->server_id(), it->address());
    }
    return {response.id(), configuration};
}

ConfigurationResult
ClientImpl::setConfiguration(uint64_t oldId,
                             const Configuration& newConfiguration)
{
    Protocol::Client::SetConfiguration::Request request;
    request.set_old_id(oldId);
    for (auto it = newConfiguration.begin();
         it != newConfiguration.end();
         ++it) {
        Protocol::Client::Server* s = request.add_new_servers();
        s->set_server_id(it->first);
        s->set_address(it->second);
    }
    Protocol::Client::SetConfiguration::Response response;
    leaderRPC->call(OpCode::SET_CONFIGURATION, request, response);
    ConfigurationResult result;
    if (response.has_ok()) {
        return result;
    }
    if (response.has_configuration_changed()) {
        result.status = ConfigurationResult::CHANGED;
        return result;
    }
    if (response.has_configuration_bad()) {
        result.status = ConfigurationResult::BAD;
        for (auto it = response.configuration_bad().bad_servers().begin();
             it != response.configuration_bad().bad_servers().end();
             ++it) {
            result.badServers.emplace_back(it->server_id(), it->address());
        }
        return result;
    }
    PANIC("Did not understand server response to append RPC:\n%s",
          Core::ProtoBuf::dumpString(response).c_str());
}

namespace {
/**
 * Parse an error response out of a ProtoBuf and into a Result object.
 */
template<typename Message>
Result
treeError(const Message& response)
{
    Result result;
    result.status = static_cast<Status>(response.status());
    result.error = response.error();
    return result;
}

/**
 * If the client has specified a condition for the operation, serialize it into
 * the request message.
 */
template<typename Message>
void
setCondition(Message& request, const Condition& condition)
{
    if (!condition.first.empty()) {
        request.mutable_condition()->set_path(condition.first);
        request.mutable_condition()->set_contents(condition.second);
    }
}

/**
 * Split a path into its components. Helper for ClientImpl::canonicalize.
 * \param[in] path
 *      Forward slash-delimited path (relative or absolute).
 * \param[out] components
 *      The components of path are appended to this vector.
 */
void
split(const std::string& path, std::vector<std::string>& components)
{
    std::string word;
    for (auto it = path.begin(); it != path.end(); ++it) {
        if (*it == '/') {
            if (!word.empty()) {
                components.push_back(word);
                word.clear();
            }
        } else {
            word += *it;
        }
    }
    if (!word.empty())
        components.push_back(word);
}

} // anonymous namespace

Result
ClientImpl::canonicalize(const std::string& path,
                         const std::string& workingDirectory,
                         std::string& canonical)
{
    canonical = "";
    std::vector<std::string> components;
    if (!path.empty() && *path.begin() != '/') {
        if (workingDirectory.empty() || *workingDirectory.begin() != '/') {
            Result result;
            result.status = Status::INVALID_ARGUMENT;
            result.error = Core::StringUtil::format(
                        "Can't use relative path '%s' from working directory "
                        "'%s' (working directory should be an absolute path)",
                        path.c_str(),
                        workingDirectory.c_str());
            return result;

        }
        split(workingDirectory, components);
    }
    split(path, components);
    // Iron out any ".", ".."
    size_t i = 0;
    while (i < components.size()) {
        if (components.at(i) == "..") {
            if (i > 0) {
                // erase previous and ".." components
                components.erase(components.begin() + i - 1,
                                 components.begin() + i + 1);
                --i;
            } else {
                Result result;
                result.status = Status::INVALID_ARGUMENT;
                result.error = Core::StringUtil::format(
                            "Path '%s' from working directory '%s' attempts "
                            "to look up directory above root ('/')",
                            path.c_str(),
                            workingDirectory.c_str());
                return result;
            }
        } else if (components.at(i) == ".") {
            components.erase(components.begin() + i);
        } else {
            ++i;
        }
    }
    if (components.empty()) {
        canonical = "/";
    } else {
        for (auto it = components.begin(); it != components.end(); ++it)
            canonical += "/" + *it;
    }
    return Result();
}

Result
ClientImpl::makeDirectory(const std::string& path,
                          const std::string& workingDirectory,
                          const Condition& condition)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() = exactlyOnceRPCHelper.getRPCInfo();
    setCondition(request, condition);
    request.mutable_make_directory()->set_path(realPath);
    Protocol::Client::ReadWriteTree::Response response;
    leaderRPC->call(OpCode::READ_WRITE_TREE, request, response);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

Result
ClientImpl::listDirectory(const std::string& path,
                          const std::string& workingDirectory,
                          const Condition& condition,
                          std::vector<std::string>& children)
{
    children.clear();
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadOnlyTree::Request request;
    setCondition(request, condition);
    request.mutable_list_directory()->set_path(realPath);
    Protocol::Client::ReadOnlyTree::Response response;
    leaderRPC->call(OpCode::READ_ONLY_TREE, request, response);
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    children = std::vector<std::string>(
                    response.list_directory().child().begin(),
                    response.list_directory().child().end());
    return Result();
}

Result
ClientImpl::removeDirectory(const std::string& path,
                            const std::string& workingDirectory,
                            const Condition& condition)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() = exactlyOnceRPCHelper.getRPCInfo();
    setCondition(request, condition);
    request.mutable_remove_directory()->set_path(realPath);
    Protocol::Client::ReadWriteTree::Response response;
    leaderRPC->call(OpCode::READ_WRITE_TREE, request, response);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

Result
ClientImpl::write(const std::string& path,
                  const std::string& workingDirectory,
                  const std::string& contents,
                  const Condition& condition)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() = exactlyOnceRPCHelper.getRPCInfo();
    setCondition(request, condition);
    request.mutable_write()->set_path(realPath);
    request.mutable_write()->set_contents(contents);
    Protocol::Client::ReadWriteTree::Response response;
    leaderRPC->call(OpCode::READ_WRITE_TREE, request, response);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

Result
ClientImpl::read(const std::string& path,
                 const std::string& workingDirectory,
                 const Condition& condition,
                 std::string& contents)
{
    contents = "";
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadOnlyTree::Request request;
    setCondition(request, condition);
    request.mutable_read()->set_path(realPath);
    Protocol::Client::ReadOnlyTree::Response response;
    leaderRPC->call(OpCode::READ_ONLY_TREE, request, response);
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    contents = response.read().contents();
    return Result();
}

Result
ClientImpl::removeFile(const std::string& path,
                       const std::string& workingDirectory,
                       const Condition& condition)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() = exactlyOnceRPCHelper.getRPCInfo();
    setCondition(request, condition);
    request.mutable_remove_file()->set_path(realPath);
    Protocol::Client::ReadWriteTree::Response response;
    leaderRPC->call(OpCode::READ_WRITE_TREE, request, response);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

void
ClientImpl::keepAlive()
{
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() = exactlyOnceRPCHelper.getRPCInfo();
    setCondition(request,
                 {"keepalive",
                 "this is just a no-op to keep the client's session active; "
                 "the condition is expected to fail"});
    request.mutable_write()->set_path("keepalive");
    request.mutable_write()->set_contents("you shouldn't see this!");
    Protocol::Client::ReadWriteTree::Response response;
    leaderRPC->call(OpCode::READ_WRITE_TREE, request, response);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::CONDITION_NOT_MET) {
        WARNING("Keep-alive write should have failed its condition. "
                "Unexpected status was: %s",
                response.error().c_str());
    }
}

uint32_t
ClientImpl::negotiateRPCVersion()
{
    Protocol::Client::GetSupportedRPCVersions::Request request;
    Protocol::Client::GetSupportedRPCVersions::Response response;
    leaderRPC->call(OpCode::GET_SUPPORTED_RPC_VERSIONS,
                    request, response);
    uint32_t serverMin = response.min_version();
    uint32_t serverMax = response.max_version();
    if (MAX_RPC_PROTOCOL_VERSION < serverMin) {
        PANIC("This client is too old to talk to your LogCabin cluster. "
              "You'll need to update your LogCabin client library. The "
              "server supports down to version %u, but this library only "
              "supports up to version %u.",
              serverMin, MAX_RPC_PROTOCOL_VERSION);

    } else if (MIN_RPC_PROTOCOL_VERSION > serverMax) {
        PANIC("This client is too new to talk to your LogCabin cluster. "
              "You'll need to upgrade your LogCabin cluster or downgrade "
              "your LogCabin client library. The server supports up to "
              "version %u, but this library only supports down to version %u.",
              serverMax, MIN_RPC_PROTOCOL_VERSION);
    } else {
        // There exists a protocol version both the client and server speak.
        // The preferred one is the maximum one they both support.
        return std::min(MAX_RPC_PROTOCOL_VERSION, serverMax);
    }
}


} // namespace LogCabin::Client
} // namespace LogCabin
