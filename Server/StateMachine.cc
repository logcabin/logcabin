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
#include "Server/SnapshotFile.h"
#include "Server/StateMachine.h"
#include "Tree/ProtoBuf.h"

namespace LogCabin {
namespace Server {


namespace PC = LogCabin::Protocol::Client;
static const uint64_t NO_ENTRY_ID = ~0UL;

StateMachine::StateMachine(std::shared_ptr<Consensus> consensus)
    : consensus(consensus)
    , mutex()
    , cond()
    , lastEntryId(0)
    , sessions()
    , nextLogId(1)
    , logNames()
    , logs()
    , tree()
    , thread(&StateMachine::threadMain, this)
{
}

StateMachine::~StateMachine()
{
    consensus->exit();
    thread.join();
}

bool
StateMachine::getResponse(const PC::ExactlyOnceRPCInfo& rpcInfo,
                          PC::CommandResponse& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto sessionIt = sessions.find(rpcInfo.client_id());
    if (sessionIt == sessions.end()) {
        WARNING("Client session expired but client still active");
        return false;
    }
    const Session& session = sessionIt->second;
    auto responseIt = session.responses.find(rpcInfo.rpc_number());
    if (responseIt == session.responses.end()) {
        // The response for this RPC has already been removed: the client is
        // not waiting for it. This request is just a duplicate that is safe to
        // drop.
        return false;
    }
    response = responseIt->second;
    return true;
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
StateMachine::readOnlyTreeRPC(const PC::ReadOnlyTree::Request& request,
                              PC::ReadOnlyTree::Response& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    Tree::ProtoBuf::readOnlyTreeRPC(tree, request, response);
}

void
StateMachine::threadMain()
{
    Core::ThreadId::setName("StateMachine");
    try {
        while (true) {
            Consensus::Entry entry = consensus->getNextEntry(lastEntryId);
            std::unique_lock<std::mutex> lockGuard(mutex);
            switch (entry.type) {
                case Consensus::Entry::SKIP:
                    break;
                case Consensus::Entry::DATA:
                    advance(entry.entryId, entry.data);
                    break;
                case Consensus::Entry::SNAPSHOT:
                    NOTICE("Loading snapshot through entry %lu into "
                           "state machine", entry.entryId);
                    tree.loadSnapshot(entry.snapshotReader->getStream());
                    break;
            }
            lastEntryId = entry.entryId;
            cond.notify_all();

            // Take a snapshot.
            // TODO(ongaro): snapshotting after every log entry is unreasonable
            // TODO(ongaro): snapshotting synchronously is unreasonable
            std::unique_ptr<SnapshotFile::Writer> writer =
                consensus->beginSnapshot(lastEntryId);
            tree.dumpSnapshot(writer->getStream());
            consensus->snapshotDone(lastEntryId);
        }
    } catch (const ThreadInterruptedException& e) {
        VERBOSE("exiting");
    }
}

bool
StateMachine::ignore(const PC::ExactlyOnceRPCInfo& rpcInfo) const
{
    auto it = sessions.find(rpcInfo.client_id());
    if (it == sessions.end())
        return true; // no such session
    const Session& session = it->second;
    if (session.responses.find(rpcInfo.rpc_number()) !=
        session.responses.end()) {
        return true; // response exists
    }
    if (rpcInfo.rpc_number() < session.firstOutstandingRPC)
        return true; // response already discarded
    return false;
}

void
StateMachine::advance(uint64_t entryId, const std::string& data)
{
    PC::Command command = Core::ProtoBuf::fromString<PC::Command>(data);
    PC::CommandResponse commandResponse;
    PC::ExactlyOnceRPCInfo rpcInfo;
    if (command.has_open_log()) {
        rpcInfo = command.open_log().exactly_once();
        if (ignore(rpcInfo))
            return;
        openLog(command.open_log(), *commandResponse.mutable_open_log());
    } else if (command.has_delete_log()) {
        rpcInfo = command.delete_log().exactly_once();
        if (ignore(rpcInfo))
            return;
        deleteLog(command.delete_log(), *commandResponse.mutable_delete_log());
    } else if (command.has_append()) {
        rpcInfo = command.append().exactly_once();
        if (ignore(rpcInfo))
            return;
        append(command.append(), *commandResponse.mutable_append());
    } else if (command.has_tree()) {
        rpcInfo = command.tree().exactly_once();
        if (ignore(rpcInfo))
            return;
        readWriteTreeRPC(command.tree(), *commandResponse.mutable_tree());
    } else if (command.has_open_session()) {
        openSession(entryId, command.open_session());
        return;
    } else {
        PANIC("unknown command at %lu: %s", entryId, data.c_str());
    }

    Session& session = sessions[rpcInfo.client_id()];
    if (session.firstOutstandingRPC < rpcInfo.rpc_number())
        session.firstOutstandingRPC = rpcInfo.rpc_number();

    // Discard unneeded responses in session
    auto it = session.responses.begin();
    while (it != session.responses.end()) {
        if (it->first < rpcInfo.first_outstanding_rpc())
            it = session.responses.erase(it);
        else
            ++it;
    }
    // Add new response to session
    session.responses[rpcInfo.rpc_number()] = commandResponse;
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
        expectedId = request.expected_entry_id();
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

void
StateMachine::readWriteTreeRPC(const PC::ReadWriteTree::Request& request,
                               PC::ReadWriteTree::Response& response)
{
    Tree::ProtoBuf::readWriteTreeRPC(tree, request, response);
}

void
StateMachine::openSession(
        uint64_t entryId,
        const Protocol::Client::OpenSession::Request& request)
{
    uint64_t clientId = entryId;
    sessions.insert({clientId, {}});
}

} // namespace LogCabin::Server
} // namespace LogCabin
