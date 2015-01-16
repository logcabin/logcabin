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

#include <signal.h>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Protocol/Common.h"
#include "RPC/Server.h"
#include "Server/RaftService.h"
#include "Server/RaftConsensus.h"
#include "Server/ClientService.h"
#include "Server/Globals.h"
#include "Server/StateMachine.h"

namespace LogCabin {
namespace Server {

////////// Globals::SigIntHandler //////////

Globals::ExitHandler::ExitHandler(Event::Loop& eventLoop, int signalNumber)
    : Signal(signalNumber)
    , eventLoop(eventLoop)
{
}

void
Globals::ExitHandler::handleSignalEvent()
{
    NOTICE("%s: shutting down", strsignal(signalNumber));
    eventLoop.exit();
}

////////// Globals //////////

Globals::Globals()
    : config()
    , eventLoop()
    , sigIntBlocker(SIGINT)
    , sigTermBlocker(SIGTERM)
    , sigUsr1Blocker(SIGUSR1)
    , sigIntHandler(eventLoop, SIGINT)
    , sigIntMonitor(eventLoop, sigIntHandler)
    , sigTermHandler(eventLoop, SIGTERM)
    , sigTermMonitor(eventLoop, sigTermHandler)
    , serverStats(*this)
    , raft()
    , stateMachine()
    , raftService()
    , clientService()
    , rpcServer()
{
}

Globals::~Globals()
{
    // LogManager assumes it and its logs have no active users when it is
    // destroyed. Currently, the only user is clientService, and this is
    // guaranteed by clientService's destructor.
}

void
Globals::init(uint64_t serverId)
{
    {
        ServerStats::Lock serverStatsLock(serverStats);
        serverStatsLock->set_server_id(serverId);
        // TODO(ongaro): write entire 'config' into serverStats
    }
    if (!raft) {
        raft.reset(new RaftConsensus(*this));
    }

    if (!raftService) {
        raftService.reset(new RaftService(*this));
    }

    if (!clientService) {
        clientService.reset(new ClientService(*this));
    }

    if (!rpcServer) {
        rpcServer.reset(new RPC::Server(eventLoop,
                                        Protocol::Common::MAX_MESSAGE_LENGTH));

        uint32_t maxThreads = config.read<uint16_t>("maxThreads", 16);
        rpcServer->registerService(Protocol::Common::ServiceId::RAFT_SERVICE,
                                   raftService,
                                   maxThreads);
        rpcServer->registerService(Protocol::Common::ServiceId::CLIENT_SERVICE,
                                   clientService,
                                   maxThreads);


        std::string configServers = config.read<std::string>("servers", "");
        std::vector<std::string> listenAddresses =
            Core::StringUtil::split(configServers, ';');
        if (listenAddresses.empty()) {
            PANIC("No server addresses specified to listen on. "
                  "You must set the 'servers' configuration option.");
        }
        std::string error = "Server ID has no matching address";
        for (uint32_t i = 0; i < listenAddresses.size(); ++i) {
            if (serverId != 0 && serverId != i + 1)
                continue;
            RPC::Address address(listenAddresses[i],
                                 Protocol::Common::DEFAULT_PORT);
            address.refresh(RPC::Address::TimePoint::max());
            error = rpcServer->bind(address);
            if (error.empty()) {
                NOTICE("Serving on %s", address.toString().c_str());
                raft->serverId = i + 1;
                raft->serverAddress = listenAddresses[i];
                {
                    ServerStats::Lock serverStatsLock(serverStats);
                    serverStatsLock->set_server_id(raft->serverId);
                    serverStatsLock->set_address(raft->serverAddress);
                }
                Core::Debug::processName =
                    Core::StringUtil::format("%lu", raft->serverId);
                raft->init();
                break;
            }
        }
        if (!error.empty()) {
            PANIC("Could not bind to any server address in: %s. "
                  "Last error was: %s",
                  configServers.c_str(),
                  error.c_str());
        }
    }

    if (!stateMachine) {
        stateMachine.reset(new StateMachine(raft, config));
    }

    serverStats.enable();
}

void
Globals::run()
{
    eventLoop.runForever();
}

} // namespace LogCabin::Server
} // namespace LogCabin
