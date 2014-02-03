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

"""This initializes a LogCabin cluster's log. You should run this exactly once on
a single server at the beginning of time when setting up a new cluster. This
script will seed the first server's configuration with its own address. From
there, you should use the cluster reconfiguration mechanism to grow the cluster
to the desired size.

Usage:
  initlog.py --serverid <id> --address <address> --storage <path>
  initlog.py (-h | --help)

Options:
  -h --help            Show this help message and exit.
  --serverid=<id>      Numeric ID of the first server in the cluster. Make
                       something up, but it better be unique. 1 is a good
                       choice.
  --address=<address>  Network address at which other servers will be able to
                       contact this server, e.g., 192.168.0.1:61023.
  --storage=<path>     Filesystem path at which the log and snapshots will be
                       stored. Set this the same as in your config file.
"""

from __future__ import print_function
from docopt import docopt
import glob
import hashlib
import os
import sys

if __name__ == '__main__':
    arguments = docopt(__doc__)
    server_id = int(arguments['--serverid'])
    address = arguments['--address']
    storagePath = arguments['--storage']

    if (glob.glob('%s/*' % storagePath)):
        print('Error: files found in storagePath, exiting', file=sys.stderr)
        sys.exit(1)

    logPath = '%s/server%d/log' % (storagePath, server_id)
    print('Creating directory: %s' % logPath)
    os.mkdir(storagePath)
    os.mkdir('%s/server%d' % (storagePath, server_id))
    os.mkdir(logPath)

    def write(filename, contents):
        filename = '%s/%s' % (logPath, filename)
        print('Writing: %s' % filename)
        checksum = 'SHA-1:%s\0' % hashlib.sha1(contents).hexdigest()
        open(filename, 'w').write(checksum + contents)

    metadata = """
version: 1
entries_start: 1
entries_end: 1
raft_metadata: {
    current_term: 1
    voted_for: 1
}
"""
    write('metadata1', metadata)
    write('metadata2', metadata)
    write('0000000000000001', """
term: 1
type: CONFIGURATION
configuration {
  prev_configuration {
    servers {
      server_id: %d
      address: "%s"
    }
  }
}
""" % (server_id, address))
    print('Done. Now go add more servers to your cluster using the '
          'reconfiguration mechanism.')

