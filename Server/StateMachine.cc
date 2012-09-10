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

#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/ThreadId.h"
#include "RPC/ProtoBuf.h"
#include "Server/Consensus.h"
#include "Server/StateMachine.h"

namespace LogCabin {
namespace Server {


namespace PC = LogCabin::Protocol::Client;
static const uint64_t NO_ENTRY_ID = ~0UL;

StateMachine::StateMachine(std::shared_ptr<Consensus> consensus)
    : consensus(consensus)
    , mutex()
    , cond()
    , thread(&StateMachine::threadMain, this)
    , lastEntryId(0)
    , responses()
    , nextLogId(1)
    , logNames()
    , logs()
{
}

StateMachine::~StateMachine()
{
    consensus->exit();
    thread.join();
}

PC::CommandResponse
StateMachine::getResponse(uint64_t id) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    while (lastEntryId < id)
        cond.wait(lockGuard);
    return responses.at(id);
}

void
StateMachine::wait(uint64_t entryId) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    while (lastEntryId < entryId)
        cond.wait(lockGuard);
}

void
StateMachine::listLogs(const PC::ListLogs::Request& request,
                       PC::ListLogs::Response& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    for (auto it = logNames.begin(); it != logNames.end(); ++it)
        response.add_log_names(it->first);
}

void
StateMachine::read(const PC::Read::Request& request,
                   PC::Read::Response& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto logIt = logs.find(request.log_id());
    if (logIt == logs.end()) {
        response.mutable_log_disappeared();
        return;
    }
    Log& log = *logIt->second;
    response.mutable_ok();
    for (auto it = log.begin(); it != log.end(); ++it) {
        if (it->entry_id() < request.from_entry_id())
            continue;
        *response.mutable_ok()->add_entry() = *it;
    }
}

void
StateMachine::getLastId(const PC::GetLastId::Request& request,
                        PC::GetLastId::Response& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto logIt = logs.find(request.log_id());
    if (logIt == logs.end()) {
        response.mutable_log_disappeared();
        return;
    }
    Log& log = *logIt->second;
    if (log.empty())
        response.mutable_ok()->set_head_entry_id(NO_ENTRY_ID);
    else
        response.mutable_ok()->set_head_entry_id(log.size());
}

void
StateMachine::threadMain()
{
    Core::ThreadId::setName("StateMachine");
    try {
        while (true) {
            Consensus::Entry entry = consensus->getNextEntry(lastEntryId);
            std::unique_lock<std::mutex> lockGuard(mutex);
            if (entry.hasData)
                advance(entry.entryId, entry.data);
            lastEntryId = entry.entryId;
            cond.notify_all();
        }
    } catch (const ThreadInterruptedException& e) {
        VERBOSE("exiting");
    }
}


void
StateMachine::advance(uint64_t entryId, const std::string& data)
{
    PC::Command command = Core::ProtoBuf::fromString<PC::Command>(data);
    PC::CommandResponse& commandResponse = responses[entryId];
    if (command.has_open_log()) {
        openLog(*command.mutable_open_log(),
                *commandResponse.mutable_open_log());
    } else if (command.has_delete_log()) {
        deleteLog(*command.mutable_delete_log(),
                  *commandResponse.mutable_delete_log());
    } else if (command.has_append()) {
        append(*command.mutable_append(),
               *commandResponse.mutable_append());
    } else {
        PANIC("unknown command at %lu: %s", entryId, data.c_str());
    }
}

void
StateMachine::openLog(const PC::OpenLog::Request& request,
                      PC::OpenLog::Response& response)
{
    auto it = logNames.find(request.log_name());
    uint64_t logId;
    if (it != logNames.end()) {
        logId = it->second;
    } else {
        logId = nextLogId;
        ++nextLogId;
        logNames.insert({request.log_name(), logId});
        logs.insert({logId, std::make_shared<Log>()});
    }
    response.set_log_id(logId);
}

void
StateMachine::deleteLog(const PC::DeleteLog::Request& request,
                        PC::DeleteLog::Response& response)
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto it = logNames.find(request.log_name());
    if (it == logNames.end())
        return;
    uint64_t logId = it->second;
    logNames.erase(it);
    logs.erase(logId);
}

void
StateMachine::append(const PC::Append::Request& request,
                     PC::Append::Response& response)
{
    auto logIt = logs.find(request.log_id());
    if (logIt == logs.end()) {
        response.mutable_log_disappeared();
        return;
    }
    Log& log = *logIt->second;
    uint64_t newId = log.size();
    uint64_t expectedId = NO_ENTRY_ID;
    if (request.has_expected_entry_id())
        expectedId = request.has_expected_entry_id();
    if (expectedId != NO_ENTRY_ID && expectedId != newId) {
        response.mutable_ok()->set_entry_id(NO_ENTRY_ID);
        return;
    }
    Entry entry;
    entry.set_entry_id(newId);
    *entry.mutable_invalidates() = request.invalidates();
    if (request.has_data())
        entry.set_data(request.data());
    log.push_back(entry);
    response.mutable_ok()->set_entry_id(newId);
}

} // namespace LogCabin::Server
} // namespace LogCabin
