#!/bin/sh

set -eu

host_system=$1
linker_flags=$2

case $host_system in
Darwin)
	expected_flags=-Wl,-dead_strip
	;;
*)
	expected_flags=-Wl,--gc-sections
	;;
esac

if [ "$linker_flags" != "$expected_flags" ]; then
	echo "file-owner linker flags: $host_system selected $linker_flags; expected $expected_flags" >&2
	exit 1
fi
