Overview
========

LogCabin is a distributed system that provides a small amount of highly
replicated, consistent storage. It is a reliable place for other distributed
systems to store their core metadata and is helpful in solving cluster
management issues. LogCabin is still in early stages of development and is not
yet recommended for actual use.

For more info, visit the project page at
https://ramcloud.stanford.edu/wiki/display/logcabin/LogCabin

This README will walk you through how to compile and run LogCabin.

Building
========

Pre-requisites:

- git
- scons
- doxygen
- g++ >= 4.4
- protobuf
- crypto++
- libevent2

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

Testing
=======

It's a good idea to run the included unit tests before proceeding::

 build/test/test

Running
=======

You first need a config file. You can base yours off of sample.conf::

 cp sample.conf logcabin.conf
 $EDITOR logcabin.conf

Now you're ready to run the server binary::

 build/LogCabin

If you want, you can run a sample application. For now, it assumes your server
is listening on localhost on port 61023::

 build/Examples/HelloWorld

If you have your own application, you can link it against
``build/liblogcabin.a``. You'll also need to link against the following
libraries:

- pthread
- protobuf
- cryptopp
- event_core
- event_pthreads

Documentation
=============

To build the documentation from the source code, run::

 scons docs

The resulting HTML files will be placed in ``docs/doxygen``.

