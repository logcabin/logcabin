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

#include <iostream>

#include "Client/Client.h"

namespace {

using LogCabin::Client::Cluster;
using LogCabin::Client::Configuration;
using LogCabin::Client::ConfigurationResult;

void
printConfiguration(const std::pair<uint64_t, Configuration>& configuration)
{
    std::cout << "Configuration " << configuration.first << ":" << std::endl;
    for (auto it = configuration.second.begin();
         it != configuration.second.end();
         ++it) {
        std::cout << "- " << it->first << ": " << it->second << std::endl;
    }
    std::cout << std::endl;
}


} // anonymous namespace

int
main(int argc, char** argv)
{
    Cluster cluster("logcabin:61023");
    std::pair<uint64_t, Configuration> configuration =
        cluster.getConfiguration();
    printConfiguration(configuration);

    uint64_t id = configuration.first;
    Configuration servers = configuration.second;
    servers.emplace_back(2, "192.168.2.2:61023");
    servers.emplace_back(3, "192.168.2.3:61023");
    servers.emplace_back(4, "192.168.2.4:61023");
    servers.emplace_back(5, "192.168.2.5:61023");
    ConfigurationResult result = cluster.setConfiguration(id, servers);

    std::cout << "Reconfiguration ";
    if (result.status == ConfigurationResult::OK) {
        std::cout << "OK" << std::endl;
    } else if (result.status == ConfigurationResult::CHANGED) {
        std::cout << "CHANGED" << std::endl;
    } else if (result.status == ConfigurationResult::BAD) {
        std::cout << "BAD SERVERS:" << std::endl;
        for (auto it = result.badServers.begin();
             it != result.badServers.end();
             ++it) {
            std::cout << "- " << it->first << ": " << it->second << std::endl;
        }
    }

    printConfiguration(cluster.getConfiguration());

}
