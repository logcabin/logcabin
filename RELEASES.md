Versioning
==========

LogCabin uses [SemVer](http://semver.org) for its version numbers. Its "public
API" consists of many components, including its network and disk formats, its
client library, and the command line arguments for various executables. These
components are all released together under a single version number, and the
release notes below describe which components have actually changed.

Release Process
===============

See [RELEASE-PROCESS.md](RELEASE-PROCESS.md).

Version 1.0.0 (2015-04-29)
==========================

This is the first stable release of LogCabin. We encourage others to try this
release out, and we believe it to be ready for production use. As it is the
very first release, users are advised to check back frequently in case serious
bugs are found.

The public API with respect to versioning consists of the following:

- `include/LogCabin/Client.h`: API
- `include/LogCabin/Debug.h`: API
- `build/LogCabin` or `/usr/bin/logcabind`: command line
- `build/Examples/Reconfigure` or `/usr/bin/logcabin-reconfigure`: command line
- `build/Examples/TreeOps` or `/usr/bin/logcabin`: command line
- config file format and options: defined by `sample.conf`
- client-to-server network protocol: compatibility
- server-to-server network protocol: compatibility
- replicated state machine behavior: compatibility
- storage layout on disk: compatibility
- snapshot format on disk: compatibility
- log format on disk of `Segmented` storage module: compatibility

Command line APIs consist of argv, zero vs nonzero exit statuses, and side
effects, but not necessarily stdout and stderr. These are documented with `-h`
and `--help` flags.

The interfaces/protocols indicating "compatibility" are not documented
publicly, but interoperability with different versions of the code is
maintained. Interoperability with third-party implementations is not
guaranteed, as there is no explicit protocol specification.

- Network protocols indicating "compatibility" provide forwards and backwards
  compatibility: older code and newer code must be able to interoperate within
  a MAJOR release. This is desirable in the network protocols to allow
  non-disruptive rolling upgrades.

- Disk formats indicating "compatibility" provide backwards compatibility:
  newer code must be able to accept formats produced by older code within a
  MAJOR release. However, older code may not be able to accept disk formats
  produced by newer code. This reflects the expectation that servers will be
  upgraded monotonically from older to newer versions but never back.

- The replicated state machine (which contains the core Tree data structure
  that clients interact with, among other things) provides backwards
  compatibility for a limited window of time. LogCabin will only update the
  externally visible behavior of its replicated state machine when all
  currently known servers support the new version. At that point, servers
  running the old version of the code may not be able to participate in the
  cluster (they will most likely PANIC repeatedly until their code is
  upgraded).


The following are specifically excluded from the public API and are not subject
to semantic versioning (they may be added to the public API in future
releases):

- client library ABI
- `scripts/logcabin-init-redhat` command line
- various other scripts
- `build/Examples/ServerStats` command line
- various other `build/Examples` executables
- `build/Storage/Tool` command line
- `ServerStats` ProtoBuf fields
- log format on disk of `SimpleFile` storage module
