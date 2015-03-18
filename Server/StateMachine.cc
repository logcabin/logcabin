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

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "build/Server/Sessions.pb.h"
#include "Core/Debug.h"
#include "Core/Mutex.h"
#include "Core/ProtoBuf.h"
#include "Core/ThreadId.h"
#include "Core/Util.h"
#include "Server/RaftConsensus.h"
#include "Server/StateMachine.h"
#include "Storage/SnapshotFile.h"
#include "Tree/ProtoBuf.h"

namespace LogCabin {
namespace Server {

namespace PC = LogCabin::Protocol::Client;

// for testing purposes
bool stateMachineSuppressThreads = false;
uint32_t stateMachineChildSleepMs = 0;

StateMachine::StateMachine(std::shared_ptr<RaftConsensus> consensus,
                           Core::Config& config)
    : consensus(consensus)
    , snapshotMinLogSize(config.read<uint64_t>("snapshotMinLogSize", 1024))
    , snapshotRatio(config.read<uint64_t>("snapshotRatio", 10))
      // TODO(ongaro): This should be configurable, but it must be the same for
      // every server, so it's dangerous to put it in the config file. Need to
      // use the Raft log to agree on this value. Also need to inform clients
      // of the value and its changes, so that they can send keep-alives at
      // appropriate intervals. For now, servers time out after about an hour,
      // and clients send keep-alives every minute.
    , sessionTimeoutNanos(1000UL * 1000 * 1000 * 60 * 60)
    , mutex()
    , entriesApplied()
    , snapshotSuggested()
    , exiting(false)
    , childPid(0)
    , lastIndex(0)
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
StateMachine::updateServerStats(Protocol::ServerStats& serverStats) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    serverStats.clear_state_machine();
    Protocol::ServerStats::StateMachine& smStats =
        *serverStats.mutable_state_machine();
    smStats.set_snapshotting(childPid != 0);
    smStats.set_last_applied(lastIndex);
    smStats.set_num_sessions(sessions.size());
}

void
StateMachine::wait(uint64_t index) const
{
    std::unique_lock<std::mutex> lockGuard(mutex);
    while (lastIndex < index)
        entriesApplied.wait(lockGuard);
}


////////// StateMachine private methods //////////

void
StateMachine::apply(const RaftConsensus::Entry& entry)
{
    PC::Command command;
    if (!Core::ProtoBuf::parse(entry.command, command)) {
        PANIC("Failed to parse protobuf for entry %lu",
              entry.index);
    }
    Session* session = NULL;
    if (command.has_tree()) {
        PC::ExactlyOnceRPCInfo rpcInfo = command.tree().exactly_once();
        auto it = sessions.find(rpcInfo.client_id());
        if (it == sessions.end()) {
            // session does not exist
        } else {
            // session exists
            session = &it->second;
            expireResponses(*session, rpcInfo.first_outstanding_rpc());
            if (rpcInfo.rpc_number() < session->firstOutstandingRPC) {
                // response already discarded, do not re-apply
            } else {
                auto inserted = session->responses.insert(
                                                {rpcInfo.rpc_number(), {}});
                if (inserted.second) {
                    // response not found, apply and save it
                    Tree::ProtoBuf::readWriteTreeRPC(
                        tree,
                        command.tree(),
                        *inserted.first->second.mutable_tree());
                } else {
                    // response exists, do not re-apply
                }
            }
        }
    } else if (command.has_open_session()) {
        uint64_t clientId = entry.index;
        session = &sessions.insert({clientId, {}}).first->second;
    } else {
        PANIC("unknown command at %lu: %s",
              entry.index,
              Core::ProtoBuf::dumpString(command).c_str());
    }

    // update based on entry.clusterTime
    if (session != NULL) {
        session->lastModified = entry.clusterTime;
        session = NULL; // pointer invalidated by expireSessions()
    }
}

void
StateMachine::applyThreadMain()
{
    Core::ThreadId::setName("StateMachine");
    try {
        while (true) {
            RaftConsensus::Entry entry = consensus->getNextEntry(lastIndex);
            std::unique_lock<std::mutex> lockGuard(mutex);
            switch (entry.type) {
                case RaftConsensus::Entry::SKIP:
                    break;
                case RaftConsensus::Entry::DATA:
                    apply(entry);
                    break;
                case RaftConsensus::Entry::SNAPSHOT:
                    NOTICE("Loading snapshot through entry %lu into "
                           "state machine", entry.index);
                    loadSessionSnapshot(*entry.snapshotReader);
                    tree.loadSnapshot(*entry.snapshotReader);
                    NOTICE("Done loading snapshot");
                    break;
            }
            expireSessions(entry.clusterTime);
            lastIndex = entry.index;
            entriesApplied.notify_all();
            if (shouldTakeSnapshot(lastIndex))
                snapshotSuggested.notify_all();
        }
    } catch (const Core::Util::ThreadInterruptedException& e) {
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
StateMachine::dumpSessionSnapshot(Core::ProtoBuf::OutputStream& stream) const
{
    // dump into protobuf
    SessionsProto::Sessions sessionsProto;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        SessionsProto::Session& session = *sessionsProto.add_session();
        session.set_client_id(it->first);
        session.set_last_modified(it->second.lastModified);
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
    stream.writeMessage(sessionsProto);
}

void
StateMachine::expireResponses(Session& session, uint64_t firstOutstandingRPC)
{
    if (session.firstOutstandingRPC >= firstOutstandingRPC)
        return;
    session.firstOutstandingRPC = firstOutstandingRPC;
    auto it = session.responses.begin();
    while (it != session.responses.end()) {
        if (it->first < session.firstOutstandingRPC)
            it = session.responses.erase(it);
        else
            ++it;
    }
}

void
StateMachine::expireSessions(uint64_t clusterTime)
{
    auto it = sessions.begin();
    while (it != sessions.end()) {
        Session& session = it->second;
        uint64_t expireTime = session.lastModified + sessionTimeoutNanos;
        if (expireTime < clusterTime) {
            uint64_t diffNanos = clusterTime - session.lastModified;
            NOTICE("Expiring client %lu's session after %lu.%09lu seconds "
                   "of cluster time due to inactivity",
                   it->first,
                   diffNanos / (1000 * 1000 * 1000UL),
                   diffNanos % (1000 * 1000 * 1000UL));
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}


void
StateMachine::loadSessionSnapshot(Core::ProtoBuf::InputStream& stream)
{
    SessionsProto::Sessions sessionsProto;
    std::string error = stream.readMessage(sessionsProto);
    if (!error.empty()) {
        PANIC("couldn't read snapshot: %s", error.c_str());
    }

    sessions.clear();
    for (auto it = sessionsProto.session().begin();
         it != sessionsProto.session().end();
         ++it) {
        Session& session = sessions.insert({it->client_id(), {}})
                                                        .first->second;
        session.lastModified = it->last_modified();
        session.firstOutstandingRPC = it->first_outstanding_rpc();
        for (auto it2 = it->rpc_response().begin();
             it2 != it->rpc_response().end();
             ++it2) {
            session.responses.insert({it2->rpc_number(), it2->response()});
        }
    }
}

bool
StateMachine::shouldTakeSnapshot(uint64_t lastIncludedIndex) const
{
    SnapshotStats::SnapshotStats stats = consensus->getSnapshotStats();

    // print every 10% but not at 100% because then we'd be printing all the
    // time
    uint64_t curr = 0;
    if (lastIncludedIndex > stats.last_snapshot_index())
        curr = lastIncludedIndex - stats.last_snapshot_index();
    uint64_t prev = curr - 1;
    uint64_t logEntries = stats.last_log_index() - stats.last_snapshot_index();
    if (curr != logEntries &&
        10 * prev / logEntries != 10 * curr / logEntries) {
        NOTICE("Have applied %lu%% of the %lu total log entries",
               100 * curr / logEntries,
               logEntries);
    }

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
        if (shouldTakeSnapshot(lastIndex))
            takeSnapshot(lastIndex, lockGuard);
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
    std::unique_ptr<Storage::SnapshotFile::Writer> writer =
        consensus->beginSnapshot(lastIncludedIndex);
    // Flush the outstanding changes to the snapshot now so that they
    // aren't somehow double-flushed later.
    writer->flushToOS();
    pid_t pid = fork();
    if (pid == -1) { // error
        PANIC("Couldn't fork: %s", strerror(errno));
    } else if (pid == 0) { // child
        Core::Debug::processName += "-child";
        usleep(stateMachineChildSleepMs * 1000); // for testing purposes
        dumpSessionSnapshot(*writer);
        tree.dumpSnapshot(*writer);
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
            writer->seekToEnd();
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
