Versioning
==========

LogCabin uses [SemVer](http://semver.org) (>= 2.0.0, < 3.0.0) for its version
numbers. Its "public API" consists of many components, including its network
and disk formats, its client library, and the command line arguments for
various executables. These components are all released together under a single
version number, and the release notes below describe which components have
actually changed.


Release Procedure
=================

- Run through tests
- Update the RPM version number in `SConstruct`
- Update this document
- Tag the git commit as vMAJOR.MINOR.PATCH


Version 0.0.1 (In Development)
==============================

LogCabin has not had any releases yet, but we are working towards one: see
issue #99 (versioning). Version 0.0.1-alpha.0 is the current version number
where one is needed (it's the lowest number we could come up with).

In the first stable release, the public API with respect to versioning will
consist of the following:

- `include/LogCabin/Client.h` API
- `include/LogCabin/Debug.h` API
- `build/LogCabin` or `/usr/bin/logcabind` command line
- `build/Examples/Reconfigure` or `/usr/bin/logcabin-reconfigure` command line
- `build/Examples/TreeOps` or `/usr/bin/logcabin` command line
- config file format and options (as defined by `sample.conf`)
- client-to-server network protocol (compatibility)
- server-to-server network protocol (compatibility)
- storage layout on disk (compatibility)
- snapshot format on disk (compatibility)
- log format on disk of `Segmented` storage module (compatibility)

where:

- command line APIs consist of argv, zero vs nonzero exit statuses, and
side effects, but not necessarily stdout and stderr. These are documented
with `-h` and `--help` flags.
- "(compatibility)" indicates that the interface/protocol is not documented
publicly, but the code will be backwards compatible with prior releases, at
least within the same MAJOR version. Compatibility with third-party
implementations is not guaranteed, as there is no explicit protocol
specification.

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
