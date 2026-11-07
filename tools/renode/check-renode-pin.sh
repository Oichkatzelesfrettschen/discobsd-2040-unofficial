#!/bin/sh
# Fail unless the installed Renode is the build the two Renode gates record
# as verified.
#
# check-boot.sh and check-picocalc.sh each carry VERIFIED_RENODE and only
# note a different build, because a newer emulator is the ordinary case on a
# developer host. CI installs one pinned release (RENODE_VERSION in
# .github/workflows/firmware.yml), so there a different build is a defect in
# the pin, and this script fails on it. It also requires the two gate
# scripts to name the same build and, when RENODE_VERSION is set, that the
# build is a build of that release.
#
# Usage: sh tools/renode/check-renode-pin.sh
# RENODE names the emulator launcher (default: renode in PATH).

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RENODE=${RENODE:-renode}

verified_in() {
	sed -n 's/^VERIFIED_RENODE="\(.*\)"$/\1/p' "$SCRIPT_DIR/$1"
}

boot=$(verified_in check-boot.sh)
picocalc=$(verified_in check-picocalc.sh)
# The launcher's own status decides first: in a pipeline it would be lost
# behind the last command's.
if ! version=$("$RENODE" --version 2>/dev/null); then
	echo "check-renode-pin: $RENODE --version failed" >&2
	exit 1
fi
running=$(printf '%s\n' "$version" | sed -n 's/.*build: *//p' | head -1)

echo "check-renode-pin: check-boot.sh verified ${boot:-none}"
echo "check-renode-pin: check-picocalc.sh verified ${picocalc:-none}"
echo "check-renode-pin: running ${running:-none}"

status=0
if [ -z "$boot" ]; then
	echo "check-renode-pin: no VERIFIED_RENODE in check-boot.sh" >&2
	status=1
fi
if [ "$boot" != "$picocalc" ]; then
	echo "check-renode-pin: the two gates name different verified builds" >&2
	status=1
fi
if [ "$running" != "$boot" ]; then
	echo "check-renode-pin: the installed build is not the verified one" >&2
	status=1
fi
if [ -n "${RENODE_VERSION:-}" ]; then
	case $boot in
	"$RENODE_VERSION"+*) ;;
	*)
		echo "check-renode-pin: $boot is not a build of release $RENODE_VERSION" >&2
		status=1
		;;
	esac
fi
exit "$status"
