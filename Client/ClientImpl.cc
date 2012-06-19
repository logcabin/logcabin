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

#include "Core/Debug.h"
#include "Client/ClientImpl.h"
#include "Core/ProtoBuf.h"
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

ClientImpl::ClientImpl()
    : leaderRPC()             // set in init()
    , rpcProtocolVersion(~0U) // set in init()
    , self()                  // set in init()
{
}

void
ClientImpl::init(std::weak_ptr<ClientImpl> self,
                 const std::string& hosts,
                 std::unique_ptr<LeaderRPCBase> mockRPC)
{
    this->self = self;
    leaderRPC = std::move(mockRPC);
    if (!leaderRPC)
        leaderRPC.reset(new LeaderRPC(RPC::Address(hosts, 0)));
    rpcProtocolVersion = negotiateRPCVersion();
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


Log
ClientImpl::openLog(const std::string& logName)
{
    Protocol::Client::OpenLog::Request request;
    request.set_log_name(logName);
    Protocol::Client::OpenLog::Response response;
    leaderRPC->call(OpCode::OPEN_LOG, request, response);
    return Log(self.lock(), logName, response.log_id());
}

void
ClientImpl::deleteLog(const std::string& logName)
{
    Protocol::Client::DeleteLog::Request request;
    request.set_log_name(logName);
    Protocol::Client::DeleteLog::Response response;
    leaderRPC->call(OpCode::DELETE_LOG, request, response);
}

std::vector<std::string>
ClientImpl::listLogs()
{
    Protocol::Client::ListLogs::Request request;
    Protocol::Client::ListLogs::Response response;
    leaderRPC->call(OpCode::LIST_LOGS, request, response);
    std::vector<std::string> logNames(response.log_names().begin(),
                                      response.log_names().end());
    std::sort(logNames.begin(), logNames.end());
    return logNames;
}

EntryId
ClientImpl::append(uint64_t logId, const Entry& entry, EntryId expectedId)
{
    Protocol::Client::Append::Request request;
    request.set_log_id(logId);
    if (expectedId != NO_ID)
        request.set_expected_entry_id(expectedId);
    for (auto it = entry.invalidates.begin();
         it != entry.invalidates.end();
         ++it) {
        request.add_invalidates(*it);
    }
    if (entry.getData() != NULL)
        request.set_data(entry.getData(), entry.getLength());
    Protocol::Client::Append::Response response;
    leaderRPC->call(OpCode::APPEND, request, response);
    if (response.has_ok())
        return response.ok().entry_id();
    if (response.has_log_disappeared())
        throw LogDisappearedException();
    PANIC("Did not understand server response to append RPC:\n%s",
          Core::ProtoBuf::dumpString(response, false).c_str());
}

std::vector<Entry>
ClientImpl::read(uint64_t logId, EntryId from)
{
    Protocol::Client::Read::Request request;
    request.set_log_id(logId);
    request.set_from_entry_id(from);
    Protocol::Client::Read::Response response;
    leaderRPC->call(OpCode::READ, request, response);
    if (response.has_ok()) {
        const auto& returnedEntries = response.ok().entry();
        std::vector<Entry> entries;
        entries.reserve(returnedEntries.size());
        for (auto it = returnedEntries.begin();
             it != returnedEntries.end();
             ++it) {
            std::vector<EntryId> invalidates(it->invalidates().begin(),
                                             it->invalidates().end());
            if (it->has_data()) {
                Entry e(it->data().c_str(),
                        uint32_t(it->data().length()),
                        invalidates);
                e.id = it->entry_id();
                entries.push_back(std::move(e));
            } else {
                Entry e(invalidates);
                e.id = it->entry_id();
                entries.push_back(std::move(e));
            }
        }
        return entries;
    }
    if (response.has_log_disappeared())
        throw LogDisappearedException();
    PANIC("Did not understand server response to append RPC:\n%s",
          Core::ProtoBuf::dumpString(response, false).c_str());
}

EntryId
ClientImpl::getLastId(uint64_t logId)
{
    Protocol::Client::GetLastId::Request request;
    request.set_log_id(logId);
    Protocol::Client::GetLastId::Response response;
    leaderRPC->call(OpCode::GET_LAST_ID, request, response);
    if (response.has_ok())
        return response.ok().head_entry_id();
    if (response.has_log_disappeared())
        throw LogDisappearedException();
    PANIC("Did not understand server response to append RPC:\n%s",
          Core::ProtoBuf::dumpString(response, false).c_str());
}

} // namespace LogCabin::Client
} // namespace LogCabin
