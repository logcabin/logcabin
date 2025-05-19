#!/usr/bin/env python
# Copyright (c) 2012-2014 Stanford University
# Copyright (c) 2014-2015 Diego Ongaro
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


"""
The incentive of this test is to check the optimization of reducing the number 
AppendEntries RPCs a leader sends to follower so as to make their logs consistent.

Usage:
  reduceAppendEntries.py [options]
  reduceAppendEntries.py (-h | --help)

Options:
  -h --help            Show this help message and exit
  --binary=<cmd>       Server binary to execute [default: build/LogCabin]
  --reconf=<opts>      Additional options to pass through to the Reconfigure
                       binary. [default: '']
"""

import random
import time
import re

from docopt import docopt
from TestFramework import TestFramework

class reduceAppendEntries(TestFramework):
    def __init__(self):
        TestFramework.__init__(self)
    
    def random_server_kill(self, server_command, timeout, killinterval, launchdelay):

        start = time.time()
        lastkill = start
        tolaunch = [] # [(time to launch, server id)]

        while True:
            time.sleep(.1)

            self.sandbox.checkFailures()
            now = time.time()

            # Check if the timeout has been met
            if now - start > timeout:
                print('\nSuccess: Timeout met with no errors!')
                break

            # Check if the kill interval has been met
            if now - lastkill > killinterval:
                server_id_ip = random.choice(self.server_processes.keys())

                self._kill_server(server_id_ip)

                lastkill = now
                tolaunch.append((now + launchdelay, server_id_ip))

            # Check if lanchdelay has been met and there are servers to launch
            while tolaunch and now > tolaunch[0][0]:
                server_id_ip = tolaunch.pop(0)[1]
                self._start_server(server_command, server_id_ip)
    
    def test_cluster(self):
        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": "--timeout=3 --verbose",
                "command": "mkdir /dir1/"
            }
        )

        self._kill_server(self.server_ids_ips[1])
        self._kill_server(self.server_ids_ips[2])
        self._kill_server(self.server_ids_ips[3])

        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": " --verbose",
                "command": "mkdir /dir5/"
            },
            bg = True
        )

        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": " --verbose",
                "command": "mkdir /dir6/"
            },
            bg = True
        )

        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": " --verbose",
                "command": "mkdir /dir7/"
            },
            bg = True
        )

        self._start_server("build/LogCabin", self.server_ids_ips[1])
        self._start_server("build/LogCabin", self.server_ids_ips[2])
        self._start_server("build/LogCabin", self.server_ids_ips[3])

        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": " --verbose",
                "command": "mkdir /dir2/"
            },
            bg = True
        )

        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": " --verbose",
                "command": "mkdir /dir3/"
            },
            bg = True
        )

        self.execute_client_command(
            client_executable = "build/Examples/TreeOps",
            conf = {
                "options": " --verbose",
                "command": "mkdir /dir4/"
            },
            bg = True
        )

    def dumpStats(self):
        for _, server_ip in self.server_ids_ips:
            self.execute_client_command(
                client_executable = "build/Client/ServerControl",
                conf = {
                    "options": "--timeout=10",
                    "command": "stats dump",
                    "server_ip": server_ip,
                },
                onCluster = False,
            )
    
    def _match_string(self, match_string, line):
        m = re.search('%s: (\d+)' % match_string, line)
        if m is not None:
            print "%s: %s" % (match_string, m.group(1))
    
    def printStats(self):
        for server_id, _ in self.server_ids_ips:
            self._print_string("\nServer %d stats" % server_id)

            for line in open('debug/server_%d' % server_id):
                self._match_string('current_term', line)
                self._match_string('commit_index', line)
                self._match_string('last_log_index', line)


def main():
    # Parse command line arguments
    arguments = docopt(__doc__)

    server_command = arguments['--binary']

    reconf_opts = arguments['--reconf']
    if reconf_opts == "''":
        reconf_opts = ""

    # Run the test
    test = reduceAppendEntries()

    test.create_configs()
    test.create_folders()

    test.initialize_cluster(server_command, reconf_opts)

    test.test_cluster()
    test.dumpStats()
    test.printStats()

    test.cleanup(debug=True)

if __name__ == '__main__':
    main()
