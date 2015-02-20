/* Copyright (c) 2012-2014 Stanford University
 * Copyright (c) 2014-2015 Diego Ongaro
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
#include "Protocol/Common.h"
#include "RPC/Address.h"
#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"

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

/**
 * Wrapper around LeaderRPC::call() that repackages a timeout as a
 * ReadOnlyTree status and error message.
 */
void
treeCall(LeaderRPCBase& leaderRPC,
         LeaderRPC::OpCode opCode,
         const Protocol::Client::ReadOnlyTree::Request& request,
         Protocol::Client::ReadOnlyTree::Response& response,
         ClientImpl::TimePoint timeout)
{
    LeaderRPC::Status status;
    status = leaderRPC.call(opCode, request, response, timeout);
    switch (status) {
        case LeaderRPC::Status::OK:
            break;
        case LeaderRPC::Status::TIMEOUT:
            response.set_status(Protocol::Client::Status::TIMEOUT);
            response.set_error("Client-specified timeout elapsed");
            break;
    }
}

/**
 * Wrapper around LeaderRPC::call() that repackages a timeout as a
 * ReadWriteTree status and error message. Also checks whether getRPCInfo
 * timed out.
 */
void
treeCall(LeaderRPCBase& leaderRPC,
         LeaderRPC::OpCode opCode,
         const Protocol::Client::ReadWriteTree::Request& request,
         Protocol::Client::ReadWriteTree::Response& response,
         ClientImpl::TimePoint timeout)
{
    LeaderRPC::Status status;
    if (request.exactly_once().client_id() == 0)
        status = LeaderRPC::Status::TIMEOUT;
    else
        status = leaderRPC.call(opCode, request, response, timeout);

    switch (status) {
        case LeaderRPC::Status::OK:
            break;
        case LeaderRPC::Status::TIMEOUT:
            response.set_status(Protocol::Client::Status::TIMEOUT);
            response.set_error("Client-specified timeout elapsed");
            break;
    }
}


} // anonymous namespace

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
    , keepAliveCall()
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
        std::unique_lock<Core::Mutex> lockGuard(mutex);
        exiting = true;
        keepAliveCV.notify_all();
        if (keepAliveCall)
            keepAliveCall->cancel();
    }
    if (keepAliveThread.joinable())
        keepAliveThread.join();
}

Protocol::Client::ExactlyOnceRPCInfo
ClientImpl::ExactlyOnceRPCHelper::getRPCInfo(TimePoint timeout)
{
    std::unique_lock<Core::Mutex> lockGuard(mutex);
    return getRPCInfo(lockGuard, timeout);
}

void
ClientImpl::ExactlyOnceRPCHelper::doneWithRPC(
        const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo)
{
    std::unique_lock<Core::Mutex> lockGuard(mutex);
    doneWithRPC(rpcInfo, lockGuard);
}

Protocol::Client::ExactlyOnceRPCInfo
ClientImpl::ExactlyOnceRPCHelper::getRPCInfo(
        std::unique_lock<Core::Mutex>& lockGuard,
        TimePoint timeout)
{
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
        LeaderRPC::Status status =
            client->leaderRPC->call(OpCode::OPEN_SESSION, request, response,
                                    timeout);
        switch (status) {
            case LeaderRPC::Status::OK:
                break;
            case LeaderRPC::Status::TIMEOUT:
                rpcInfo.set_client_id(0);
                return rpcInfo;
        }
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
                    const Protocol::Client::ExactlyOnceRPCInfo& rpcInfo,
                    std::unique_lock<Core::Mutex>& lockGuard)
{
    outstandingRPCNumbers.erase(rpcInfo.rpc_number());
}

void
ClientImpl::ExactlyOnceRPCHelper::keepAliveThreadMain()
{
    std::unique_lock<Core::Mutex> lockGuard(mutex);
    while (!exiting) {
        TimePoint nextKeepAlive;
        if (keepAliveIntervalMs > 0) {
            nextKeepAlive = (lastKeepAliveStart +
                             std::chrono::milliseconds(keepAliveIntervalMs));
        } else {
            nextKeepAlive = TimePoint::max();
        }
        if (Clock::now() > nextKeepAlive) {
            Protocol::Client::ReadWriteTree::Request request;
            *request.mutable_exactly_once() = getRPCInfo(lockGuard,
                                                         TimePoint::max());
            setCondition(request,
                 {"keepalive",
                 "this is just a no-op to keep the client's session active; "
                 "the condition is expected to fail"});
            request.mutable_write()->set_path("keepalive");
            request.mutable_write()->set_contents("you shouldn't see this!");
            Protocol::Client::ReadWriteTree::Response response;
            keepAliveCall = client->leaderRPC->makeCall();
            keepAliveCall->start(OpCode::READ_WRITE_TREE, request,
                                 TimePoint::max());
            LeaderRPCBase::Call::Status callStatus;
            {
                // release lock to allow concurrent cancellation
                Core::MutexUnlock<Core::Mutex> unlockGuard(lockGuard);
                callStatus = keepAliveCall->wait(response, TimePoint::max());
            }
            keepAliveCall.reset();
            switch (callStatus) {
                case LeaderRPCBase::Call::Status::OK:
                    break;
                case LeaderRPCBase::Call::Status::RETRY:
                    continue; // retry outer loop
                case LeaderRPCBase::Call::Status::TIMEOUT:
                    PANIC("Unexpected timeout for keep-alive");
            }
            doneWithRPC(request.exactly_once(), lockGuard);
            if (response.status() !=
                Protocol::Client::Status::CONDITION_NOT_MET) {
                WARNING("Keep-alive write should have failed its condition. "
                        "Unexpected status was %d: %s",
                        response.status(),
                        response.error().c_str());
            }
            continue;
        }
        keepAliveCV.wait_until(lockGuard, nextKeepAlive);
    }
}

////////// class ClientImpl //////////

ClientImpl::ClientImpl(const std::map<std::string, std::string>& options)
    : config(options)
    , eventLoop()
    , sessionCreationBackoff(5,                   // 5 new connections per
                             100UL * 1000 * 1000) // 100 ms
    , hosts()
    , leaderRPC()             // set in init()
    , rpcProtocolVersion(~0U) // set in init()
    , exactlyOnceRPCHelper(this)
    , eventLoopThread()
{
}

ClientImpl::~ClientImpl()
{
    eventLoop.exit();
    if (eventLoopThread.joinable())
        eventLoopThread.join();
    exactlyOnceRPCHelper.exit();
}

void
ClientImpl::init(const std::string& hosts)
{
    this->hosts = hosts;
    eventLoopThread = std::thread(&Event::Loop::runForever, &eventLoop);
    initDerived();
}

void
ClientImpl::initDerived()
{
    if (!leaderRPC) { // sometimes set in unit tests
        leaderRPC.reset(new LeaderRPC(
            RPC::Address(hosts, Protocol::Common::DEFAULT_PORT),
            eventLoop,
            sessionCreationBackoff,
            config));
    }
    if (rpcProtocolVersion == ~0U)
        rpcProtocolVersion = negotiateRPCVersion();
}

std::pair<uint64_t, Configuration>
ClientImpl::getConfiguration()
{
    // TODO(ongaro):  expose timeout
    Protocol::Client::GetConfiguration::Request request;
    Protocol::Client::GetConfiguration::Response response;
    leaderRPC->call(OpCode::GET_CONFIGURATION, request, response,
                    TimePoint::max());
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
    // TODO(ongaro):  expose timeout
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
    leaderRPC->call(OpCode::SET_CONFIGURATION, request, response,
                    TimePoint::max());
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
    PANIC("Did not understand server response to setConfiguration RPC:\n%s",
          Core::ProtoBuf::dumpString(response).c_str());
}

Result
ClientImpl::getServerStats(const std::string& host,
                           TimePoint timeout,
                           Protocol::ServerStats& stats)
{
    Result timeoutResult;
    timeoutResult.status = Client::Status::TIMEOUT;
    timeoutResult.error = "Client-specified timeout elapsed";

    while (true) {
        sessionCreationBackoff.delayAndBegin(timeout);

        RPC::Address address(host, Protocol::Common::DEFAULT_PORT);
        address.refresh(timeout);

        std::shared_ptr<RPC::ClientSession> session =
            RPC::ClientSession::makeSession(
                            eventLoop,
                            address,
                            Protocol::Common::MAX_MESSAGE_LENGTH,
                            timeout,
                            config);

        Protocol::Client::GetServerStats::Request request;
        RPC::ClientRPC rpc(session,
                           Protocol::Common::ServiceId::CLIENT_SERVICE,
                           1,
                           OpCode::GET_SERVER_STATS,
                           request);

        typedef RPC::ClientRPC::Status RPCStatus;
        Protocol::Client::GetServerStats::Response response;
        Protocol::Client::Error error;
        RPCStatus status = rpc.waitForReply(&response, &error, timeout);

        // Decode the response
        switch (status) {
            case RPCStatus::OK:
                stats = response.server_stats();
                return Result();
            case RPCStatus::RPC_FAILED:
                break;
            case RPCStatus::TIMEOUT:
                return timeoutResult;
            case RPCStatus::SERVICE_SPECIFIC_ERROR:
                // Hmm, we don't know what this server is trying to tell us,
                // but something is wrong. The server shouldn't reply back with
                // error codes we don't understand. That's why we gave it a
                // serverSpecificErrorVersion number in the request header.
                PANIC("Unknown error code %u returned in service-specific "
                      "error. This probably indicates a bug in the server",
                      error.error_code());
            case RPCStatus::RPC_CANCELED:
                PANIC("RPC canceled unexpectedly");
        }
        if (timeout < Clock::now())
            return timeoutResult;
        else
            continue;
    }
}

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
                          const Condition& condition,
                          TimePoint timeout)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() =
        exactlyOnceRPCHelper.getRPCInfo(timeout);
    setCondition(request, condition);
    request.mutable_make_directory()->set_path(realPath);
    Protocol::Client::ReadWriteTree::Response response;
    treeCall(*leaderRPC, OpCode::READ_WRITE_TREE,
             request, response, timeout);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

Result
ClientImpl::listDirectory(const std::string& path,
                          const std::string& workingDirectory,
                          const Condition& condition,
                          TimePoint timeout,
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
    treeCall(*leaderRPC, OpCode::READ_ONLY_TREE,
             request, response, timeout);
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
                            const Condition& condition,
                            TimePoint timeout)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() =
        exactlyOnceRPCHelper.getRPCInfo(timeout);
    setCondition(request, condition);
    request.mutable_remove_directory()->set_path(realPath);
    Protocol::Client::ReadWriteTree::Response response;
    treeCall(*leaderRPC, OpCode::READ_WRITE_TREE,
             request, response, timeout);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

Result
ClientImpl::write(const std::string& path,
                  const std::string& workingDirectory,
                  const std::string& contents,
                  const Condition& condition,
                  TimePoint timeout)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() =
        exactlyOnceRPCHelper.getRPCInfo(timeout);
    setCondition(request, condition);
    request.mutable_write()->set_path(realPath);
    request.mutable_write()->set_contents(contents);
    Protocol::Client::ReadWriteTree::Response response;
    treeCall(*leaderRPC, OpCode::READ_WRITE_TREE,
             request, response, timeout);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

Result
ClientImpl::read(const std::string& path,
                 const std::string& workingDirectory,
                 const Condition& condition,
                 TimePoint timeout,
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
    treeCall(*leaderRPC, OpCode::READ_ONLY_TREE,
             request, response, timeout);
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    contents = response.read().contents();
    return Result();
}

Result
ClientImpl::removeFile(const std::string& path,
                       const std::string& workingDirectory,
                       const Condition& condition,
                       TimePoint timeout)
{
    std::string realPath;
    Result result = canonicalize(path, workingDirectory, realPath);
    if (result.status != Status::OK)
        return result;
    Protocol::Client::ReadWriteTree::Request request;
    *request.mutable_exactly_once() =
        exactlyOnceRPCHelper.getRPCInfo(timeout);
    setCondition(request, condition);
    request.mutable_remove_file()->set_path(realPath);
    Protocol::Client::ReadWriteTree::Response response;
    treeCall(*leaderRPC, OpCode::READ_WRITE_TREE,
             request, response, timeout);
    exactlyOnceRPCHelper.doneWithRPC(request.exactly_once());
    if (response.status() != Protocol::Client::Status::OK)
        return treeError(response);
    return Result();
}

uint32_t
ClientImpl::negotiateRPCVersion()
{
    // Doesn't seem reasonable for this to block forever: defer until first
    // RPC, and use the timeout from that? See
    // https://github.com/logcabin/logcabin/issues/76

    Protocol::Client::GetSupportedRPCVersions::Request request;
    Protocol::Client::GetSupportedRPCVersions::Response response;
    leaderRPC->call(OpCode::GET_SUPPORTED_RPC_VERSIONS,
                    request, response, TimePoint::max());
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
