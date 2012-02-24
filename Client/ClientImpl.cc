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

#include "include/Debug.h"
#include "Client/ClientImpl.h"

namespace LogCabin {
namespace Client {

using ProtoBuf::ClientRPC::OpCode;

PlaceholderRPC* placeholderRPC = NULL;

ClientImpl::ClientImpl()
    : errorCallback()
    , self()
{
}

void
ClientImpl::setSelf(std::weak_ptr<ClientImpl> self)
{
    this->self = self;
}

void
ClientImpl::registerErrorCallback(std::unique_ptr<ErrorCallback> callback)
{
    this->errorCallback = std::move(callback);
}

Log
ClientImpl::openLog(const std::string& logName)
{
    ProtoBuf::ClientRPC::OpenLog::Request request;
    request.set_log_name(logName);
    ProtoBuf::ClientRPC::OpenLog::Response response;
    placeholderRPC->leader(OpCode::OPEN_LOG, request, response);
    return Log(self.lock(), logName, response.log_id());
}

void
ClientImpl::deleteLog(const std::string& logName)
{
    ProtoBuf::ClientRPC::DeleteLog::Request request;
    request.set_log_name(logName);
    ProtoBuf::ClientRPC::DeleteLog::Response response;
    placeholderRPC->leader(OpCode::DELETE_LOG, request, response);
}

std::vector<std::string>
ClientImpl::listLogs()
{
    ProtoBuf::ClientRPC::ListLogs::Request request;
    ProtoBuf::ClientRPC::ListLogs::Response response;
    placeholderRPC->leader(OpCode::LIST_LOGS, request, response);
    std::vector<std::string> logNames(response.log_names().begin(),
                                      response.log_names().end());
    std::sort(logNames.begin(), logNames.end());
    return logNames;
}

EntryId
ClientImpl::append(uint64_t logId, const Entry& entry, EntryId previousId)
{
    ProtoBuf::ClientRPC::Append::Request request;
    request.set_log_id(logId);
    if (previousId != NO_ID)
        request.set_previous_entry_id(previousId);
    for (auto it = entry.invalidates.begin();
         it != entry.invalidates.end();
         ++it) {
        request.add_invalidates(*it);
    }
    if (entry.getData() != NULL)
        request.set_data(entry.getData(), entry.getLength());
    ProtoBuf::ClientRPC::Append::Response response;
    placeholderRPC->leader(OpCode::APPEND, request, response);
    if (response.has_ok())
        return response.ok().entry_id();
    if (response.has_log_disappeared())
        throw LogDisappearedException();
    PANIC("Did not understand server response to append RPC:\n%s",
          ProtoBuf::dumpString(response, false).c_str());
}

std::vector<Entry>
ClientImpl::read(uint64_t logId, EntryId from)
{
    ProtoBuf::ClientRPC::Read::Request request;
    request.set_log_id(logId);
    request.set_from_entry_id(from);
    ProtoBuf::ClientRPC::Read::Response response;
    placeholderRPC->leader(OpCode::READ, request, response);
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
          ProtoBuf::dumpString(response, false).c_str());
}

EntryId
ClientImpl::getLastId(uint64_t logId)
{
    ProtoBuf::ClientRPC::GetLastId::Request request;
    request.set_log_id(logId);
    ProtoBuf::ClientRPC::GetLastId::Response response;
    placeholderRPC->leader(OpCode::GET_LAST_ID, request, response);
    if (response.has_ok())
        return response.ok().head_entry_id();
    if (response.has_log_disappeared())
        throw LogDisappearedException();
    PANIC("Did not understand server response to append RPC:\n%s",
          ProtoBuf::dumpString(response, false).c_str());
}

} // namespace LogCabin::Client
} // namespace LogCabin
