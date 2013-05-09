#!/usr/bin/env python
# Copyright (c) 2012 Stanford University
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
This runs some basic tests against a LogCabin cluster.
"""

from __future__ import print_function, division
from common import sh, captureSh, Sandbox, smokehosts
import subprocess
import time

def run(num_servers = None, # default 5
        client_command = 'build/Examples/SmokeTest',
        timeout = 10):

    if num_servers is None:
        num_servers = min(5, len(smokehosts))
    server_ids = range(1, num_servers + 1)

    with Sandbox() as sandbox:
        sh('rm -rf log')
        sh('rm -f snapshot.*')
        sh('rm -f debug/*')
        sh('scripts/initlog.py --serverid 1 --address %s' % smokehosts[0][0])

        for server_id in server_ids:
            host = smokehosts[server_id - 1]
            command = 'build/LogCabin --id %d --config smoketest.conf' % server_id
            print('Starting %s on %s' % (command, smokehosts[server_id - 1][0]))
            sandbox.rsh(smokehosts[server_id - 1][0], command, bg=True,
                        stderr=open('debug/%d' % server_id, 'w'))
            sandbox.checkFailures()

        print('Growing cluster')
        sh('build/Examples/Reconfigure %s' %
           ' '.join([h[0] for h in smokehosts]))

        print('Starting %s on localhost' % client_command)
        client = sandbox.rsh('localhost', client_command, bg=True,
                             stderr=open('debug/client', 'w'))

        start = time.time()
        while client.proc.returncode is None:
            sandbox.checkFailures()
            time.sleep(.1)
            if time.time() - start > timeout:
                raise Exception('timeout exceeded')

if __name__ == '__main__':
    run()
