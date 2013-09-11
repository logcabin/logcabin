Overview
========

LogCabin is a distributed system that provides a small amount of highly
replicated, consistent storage. It is a reliable place for other distributed
systems to store their core metadata and is helpful in solving cluster
management issues. Although its key functionality is in place, LogCabin is not
yet recommended for actual use. LogCabin is released under the permissive ISC
license (which is equivalent to the Simplified BSD License).

For more info, visit the project page at
https://ramcloud.stanford.edu/wiki/display/logcabin/LogCabin

LogCabin uses the Raft consensus algorithm internally, which is described in
https://ramcloud.stanford.edu/raft.pdf

This README will walk you through how to compile and run LogCabin.

Building
========

Pre-requisites:

- git (v1.7 is known to work)
- scons (v2.1 is known to work)
- g++ (v4.4 and up should work)
- protobuf (v2.4.1 is known to work)
- crypto++ (v5.6.1 is known to work)
- libevent2 (v2.0.19 is known to work)
- doxygen (optional; v1.8.1 is known to work)

Get the source code::

 git clone git://github.com/logcabin/logcabin.git
 cd logcabin
 git submodule update --init


Build the client library, server binary, and unit tests::

 scons

For custom build environments, you can place your configuration variables in
``Local.sc``. For example, that file might look like::

 BUILDTYPE='DEBUG'
 CXXFLAGS=['-Wno-error']

To see which configuration parameters are available, run::

 scons --help

Running basic tests
===================

It's a good idea to run the included unit tests before proceeding::

 build/test/test

You can also run some system-wide tests. This first command runs the smoke
tests against an in-memory database that is embedded into the LogCabin client
(no servers are involved)::

 build/Examples/SmokeTest --mock && echo 'Smoke test completed successfully'

To run the same smoke test against a real LogCabin cluster, you'll need to go
through some more configuration. This can be found after the Running section.

Running a real cluster
======================

This section shows you how to run the HelloWorld example program against a
three-server LogCabin cluster. We'll run all the servers on localhost for now,
but it takes a little trickery to do this. LogCabin clients find the cluster
through DNS; since DNS doesn't support port numbers, you'll need three distinct
IP addresses. This example uses 192.168.2.1, 192.168.2.2, and 192.168.2.3,
which hopefully won't collide your actual network range.

First, create three virtual loopback interfaces::

 sudo ifconfig lo:1 192.168.2.1
 sudo ifconfig lo:2 192.168.2.2
 sudo ifconfig lo:3 192.168.2.3

Then, point the DNS name 'logcabin' to all of these by adding the following
lines to /etc/hosts::

 192.168.2.1 logcabin
 192.168.2.2 logcabin
 192.168.2.3 logcabin

Now you need a configuration file called logcabin.conf. You can base yours off
of sample.conf, or the following will work for now::

  storageModule = filesystem
  storagePath = smoketeststorage
  servers = 192.168.2.1:61023;192.168.2.2:61023;192.168.2.3:61023

LogCabin daemons use this file so they know where to store their data and what
addresses they may listen on. A server listens on the first address in the
'servers' variable that it is able to bind to (this behavior is convenient so
that you can use the same configuration file for all servers and have each one
figure out what to do).

Now you're almost ready to start the servers. First, initialize one of the
server's logs with a cluster membership configuration that contains just
itself::

  scripts/initlog.py --serverid=1 --address=192.168.2.1:61023 --storage=smoketeststorage

The server with ID 1 will now have a valid cluster membership configuration in
its log. At this point, there's only 1 server in the cluster, so only 1 vote is
needed: it'll be able to elect itself leader and commit new entries. We can now
start this server (leave it running)::

 build/LogCabin --id 1

We don't want to stop here, though, because the cluster isn't fault-tolerant
with just one server! Let's start up the second server in another terminal
(leave it running)::

 build/LogCabin --id 2

Note how this server is just idling, awaiting a cluster membership
configuration. It's still not part of the cluster.

Start the third server also (LogCabin checks to make sure all the servers in
your new configuration are available before committing to switch to it, just to
keep you from doing anything stupid)::

 build/LogCabin --id 3

Now use the reconfiguration command to add the second and third servers to the
cluster::

  build/Examples/Reconfigure 192.168.2.1:61023 192.168.2.2:61023 192.168.2.3:61023

This Reconfigure command is a special LogCabin client. It looks up the cluster
using DNS and asks the leader to reconfigure the cluster to the addresses given
on its command line. If this succeeded, you should see that the first server
has added the others to the cluster, and the second and third servers are now
participating. It should have output something like::

 Configuration 1:
 - 1: 192.168.2.1:61023
 
 Reconfiguration OK
 Configuration 4:
 - 1: 192.168.2.1:61023
 - 2: 192.168.2.2:61023
 - 3: 192.168.2.3:61023

Finally, you can run a LogCabin client to exercise the cluster::

 build/Examples/HelloWorld

That program doesn't do anything very interesting. You should be able to kill
one server at a time and maintain availability, or kill more and restart
them and maintain safety (with an availability hiccup).

If you have your own application, you can link it against
``build/liblogcabin.a``. You'll also need to link against the following
libraries:

- pthread
- protobuf
- cryptopp
- event_core
- event_pthreads

Running cluster-wide tests
==========================

The procedure described above for running a cluster is fairly tedious when you
just want to run some tests and tear everything down again. Thus,
scripts/smoketest.py automates it. Create a file called scripts/localconfig.py
to override the smokehosts and hosts variables found in scripts/config.py::

 smokehosts = hosts = [
   ('192.168.2.1', '192.168.2.1', 1),
   ('192.168.2.2', '192.168.2.2', 2),
   ('192.168.2.3', '192.168.2.3', 3),
 ]

The scripts use this file to when launching servers using SSH. Each tuple in
the (smoke)hosts list represents one server, containing:

 1. the address to use for SSH,
 2. the address to use for LogCabin TCP connections, and
 3. a unique ID.

Each of these servers should be accessible over SSH without a password and
should have the LogCabin directory available in the same filesystem location.

You will also need a corresponding smoketest.conf file::

  servers = 192.168.2.1:61023;192.168.2.2:61023;192.168.2.3:61023
  storageModule = filesystem
  storagePath = smoketeststorage

This is just like logcabin.conf but is used when running the smoke tests. Now
you're ready to run::

 scripts/smoketest.py && echo 'Smoke test completed successfully'

This script can easily be hijacked/included to run other test programs.

Documentation
=============

To build the documentation from the source code, run::

 scons docs

The resulting HTML files will be placed in ``docs/doxygen``.

The Raft consensus algorithm is described in
https://ramcloud.stanford.edu/raft.pdf

Contributing
============

Please use the github to report issues and send pull requests.

Each commit should pass the pre-commit hooks. Enable them to run before each
commit::

 ln -s ../../hooks/pre-commit .git/hooks/pre-commit
