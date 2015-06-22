#!/bin/sh

# Smoke test for logcabinctl program.

set -ex

ctl=build/Client/ServerControl
tmpdir=$(mktemp -d)

cat >logcabin-1.conf << EOF
serverId = 1
listenAddresses = localhost:5254
storagePath=$tmpdir
EOF

mkdir -p debug
rm -rf debug/1
rm -rf debug/2

build/LogCabin --config logcabin-1.conf --bootstrap --log debug/1

build/LogCabin --config logcabin-1.conf --log debug/1 &
pid=$!

$ctl info get
$ctl debug filename get
$ctl debug filename set debug/2
! $ctl debug filename set /a/b/c/d/e/f/g
$ctl debug rotate
$ctl stats get
$ctl stats dump

kill $pid
wait

rm -r $tmpdir
