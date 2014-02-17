/* Copyright (c) 2012-2013 Stanford University
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "build/Server/Sessions.pb.h"
#include "Core/Debug.h"
#include "Core/Mutex.h"
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

// for testing purposes
bool stateMachineSuppressThreads = false;
uint32_t stateMachineChildSleepMs = 0;

StateMachine::StateMachine(std::shared_ptr<Consensus> consensus,
                           Core::Config& config)
    : consensus(consensus)
    , snapshotMinLogSize(config.read<uint64_t>("snapshotMinLogSize", 1024))
    , snapshotRatio(config.read<uint64_t>("snapshotRatio", 10))
    , mutex()
    , entriesApplied()
    , snapshotSuggested()
    , exiting(false)
    , childPid(0)
    , lastEntryId(0)
    , sessions()
    , tree()
    , applyThread()
    , snapshotThread()
{
    if (!stateMachineSuppressThreads) {
        applyThread = std::thread(&StateMachine::applyThreadMain, this);
        snapshotThread = std::thread(&StateMachine::snapshotThreadMain, this);
    }
}

StateMachine::~StateMachine()
{
    NOTICE("Shutting down");
    if (consensus) // sometimes missing for testing
        consensus->exit();
    if (applyThread.joinable())
        applyThread.join();
    if (snapshotThread.joinable())
        snapshotThread.join();
    NOTICE("Joined with threads");
}

bool
StateMachine::getResponse(const PC::ExactlyOnceRPCInfo& rpcInfo,
                          PC::CommandResponse& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    auto sessionIt = sessions.find(rpcInfo.client_id());
    if (sessionIt == sessions.end()) {
        WARNING("Client %lu session expired but client still active",
                rpcInfo.client_id());
        return false;
    }
    const Session& session = sessionIt->second;
    auto responseIt = session.responses.find(rpcInfo.rpc_number());
    if (responseIt == session.responses.end()) {
        // The response for this RPC has already been removed: the client is
        // not waiting for it. This request is just a duplicate that is safe to
        // drop.
        WARNING("Client %lu asking for discarded response to RPC %lu",
                rpcInfo.client_id(), rpcInfo.rpc_number());
        return false;
    }
    response = responseIt->second;
    return true;
}

void
StateMachine::readOnlyTreeRPC(const PC::ReadOnlyTree::Request& request,
                              PC::ReadOnlyTree::Response& response) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    Tree::ProtoBuf::readOnlyTreeRPC(tree, request, response);
}

void
StateMachine::wait(uint64_t entryId) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    while (lastEntryId < entryId)
        entriesApplied.wait(lockGuard);
}


////////// StateMachine private methods //////////

void
StateMachine::apply(uint64_t entryId, const std::string& data)
{
    PC::Command command = Core::ProtoBuf::fromString<PC::Command>(data);
    PC::CommandResponse commandResponse;
    PC::ExactlyOnceRPCInfo rpcInfo;
    if (command.has_tree()) {
        rpcInfo = command.tree().exactly_once();
        if (ignore(rpcInfo))
            return;
        Tree::ProtoBuf::readWriteTreeRPC(
            tree, command.tree(), *commandResponse.mutable_tree());
    } else if (command.has_open_session()) {
        openSession(entryId, command.open_session());
        return;
    } else {
        PANIC("unknown command at %lu: %s", entryId, data.c_str());
    }

    Session& session = sessions[rpcInfo.client_id()];
    if (session.firstOutstandingRPC < rpcInfo.first_outstanding_rpc())
        session.firstOutstandingRPC = rpcInfo.first_outstanding_rpc();

    // Discard unneeded responses in session
    auto it = session.responses.begin();
    while (it != session.responses.end()) {
        if (it->first < session.firstOutstandingRPC)
            it = session.responses.erase(it);
        else
            ++it;
    }
    // Add new response to session
    session.responses[rpcInfo.rpc_number()] = commandResponse;
}


void
StateMachine::applyThreadMain()
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
                    apply(entry.entryId, entry.data);
                    break;
                case Consensus::Entry::SNAPSHOT:
                    NOTICE("Loading snapshot through entry %lu into "
                           "state machine", entry.entryId);
                    loadSessionSnapshot(entry.snapshotReader->getStream());
                    tree.loadSnapshot(entry.snapshotReader->getStream());
                    break;
            }
            lastEntryId = entry.entryId;
            entriesApplied.notify_all();
            if (shouldTakeSnapshot(lastEntryId))
                snapshotSuggested.notify_all();
        }
    } catch (const ThreadInterruptedException& e) {
        NOTICE("exiting");
        std::unique_lock<std::mutex> lockGuard(mutex);
        exiting = true;
        entriesApplied.notify_all();
        snapshotSuggested.notify_all();
        if (childPid != 0) {
            int r = kill(childPid, SIGHUP);
            if (r != 0) {
                WARNING("Could not send SIGHUP to child process: %s",
                        strerror(errno));
            }
        }
    }
}

void
StateMachine::dumpSessionSnapshot(
                google::protobuf::io::CodedOutputStream& stream) const
{
    // dump into protobuf
    SessionsProto::Sessions sessionsProto;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        SessionsProto::Session& session = *sessionsProto.add_session();
        session.set_client_id(it->first);
        session.set_first_outstanding_rpc(it->second.firstOutstandingRPC);
        for (auto it2 = it->second.responses.begin();
             it2 != it->second.responses.end();
             ++it2) {
            SessionsProto::Response& response = *session.add_rpc_response();
            response.set_rpc_number(it2->first);
            *response.mutable_response() = it2->second;
        }
    }

    // write protobuf to stream
    int size = sessionsProto.ByteSize();
    stream.WriteLittleEndian32(size);
    sessionsProto.SerializeWithCachedSizes(&stream);
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
StateMachine::loadSessionSnapshot(
                google::protobuf::io::CodedInputStream& stream)
{
    // read protobuf from stream
    bool ok = true;
    uint32_t numBytes = 0;
    ok = stream.ReadLittleEndian32(&numBytes);
    if (!ok)
        PANIC("couldn't read snapshot");
    SessionsProto::Sessions sessionsProto;
    auto limit = stream.PushLimit(numBytes);
    ok = sessionsProto.MergePartialFromCodedStream(&stream);
    stream.PopLimit(limit);
    if (!ok)
        PANIC("couldn't read snapshot");

    // load from protobuf
    sessions.clear();
    for (auto it = sessionsProto.session().begin();
         it != sessionsProto.session().end();
         ++it) {
        Session& session = sessions.insert({it->client_id(), {}})
                                                        .first->second;
        session.firstOutstandingRPC = it->first_outstanding_rpc();
        for (auto it2 = it->rpc_response().begin();
             it2 != it->rpc_response().end();
             ++it2) {
            session.responses.insert({it2->rpc_number(), it2->response()});
        }
    }
}

void
StateMachine::openSession(
        uint64_t entryId,
        const Protocol::Client::OpenSession::Request& request)
{
    uint64_t clientId = entryId;
    sessions.insert({clientId, {}});
}

bool
StateMachine::shouldTakeSnapshot(uint64_t lastIncludedIndex) const
{
    SnapshotStats::SnapshotStats stats = consensus->getSnapshotStats();
    if (stats.log_bytes() < snapshotMinLogSize)
        return false;
    if (stats.log_bytes() < stats.last_snapshot_bytes() * snapshotRatio)
        return false;
    if (lastIncludedIndex < stats.last_snapshot_index())
        return false;
    if (lastIncludedIndex < stats.last_log_index() * 3 / 4)
        return false;
    return true;
}

void
StateMachine::snapshotThreadMain()
{
    Core::ThreadId::setName("SnapshotStateMachine");
    while (true) {
        std::unique_lock<std::mutex> lockGuard(mutex);
        if (exiting)
            return;
        if (shouldTakeSnapshot(lastEntryId))
            takeSnapshot(lastEntryId, lockGuard);
        else
            snapshotSuggested.wait(lockGuard);
    }
}

void
StateMachine::takeSnapshot(uint64_t lastIncludedIndex,
                           std::unique_lock<std::mutex>& lockGuard)
{
    // Open a snapshot file, then fork a child to write a consistent view of
    // the state machine to the snapshot file while this process continues
    // accepting requests.
    std::unique_ptr<SnapshotFile::Writer> writer =
        consensus->beginSnapshot(lastIncludedIndex);
    // Flush the outstanding changes to the snapshot now so that they
    // aren't somehow double-flushed later.
    writer->flushToOS();
    pid_t pid = fork();
    if (pid == -1) { // error
        PANIC("Couldn't fork: %s", strerror(errno));
    } else if (pid == 0) { // child
        usleep(stateMachineChildSleepMs * 1000); // for testing purposes
        dumpSessionSnapshot(writer->getStream());
        tree.dumpSnapshot(writer->getStream());
        // Flush the changes to the snapshot file before exiting.
        writer->flushToOS();
        _exit(0);
    } else { // parent
        assert(childPid == 0);
        childPid = pid;
        int status = 0;
        {
            // release the lock while blocking on the child to allow
            // parallelism
            Core::MutexUnlock<std::mutex> unlockGuard(lockGuard);
            pid = waitpid(pid, &status, 0);
        }
        childPid = 0;
        if (pid == -1)
            PANIC("Couldn't waitpid: %s", strerror(errno));
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            NOTICE("Child completed writing state machine contents to "
                   "snapshot staging file");
            consensus->snapshotDone(lastIncludedIndex, std::move(writer));
        } else if (exiting &&
                   WIFSIGNALED(status) && WTERMSIG(status) == SIGHUP) {
            writer->discard();
            NOTICE("Child exited from SIGHUP since this process is "
                   "exiting");
        } else {
            writer->discard();
            PANIC("Snapshot creation failed with status %d", status);
        }
    }
}

} // namespace LogCabin::Server
} // namespace LogCabin
