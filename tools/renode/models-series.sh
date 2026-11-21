#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
##
# Tie the built Renode peripheral models to the patch series they were built
# from. fetch-renode-rp2040.sh applies tools/renode/patches to a pinned
# revision and builds Peripherals.dll into a directory git ignores, so a
# checkout whose series has moved keeps a build of the earlier series, and
# every gate that boots those models would measure the earlier ones. The
# series identity is the SHA-256 of fetch-renode-rp2040.sh (which holds the
# pin) and of each patch with its file name, in order. The fetch writes it
# beside the build, and each gate compares it before it starts Renode.
#
# Usage: models-series.sh id
#        models-series.sh stamp MODELS
#        models-series.sh check MODELS
#        models-series.sh selftest
#
# id prints the identity of the series in this directory. stamp writes it to
# MODELS.series. check exits 0 when MODELS.series holds it, 1 with the
# reason on standard error when the stamp is absent or names another series,
# and 2 when the identity cannot be computed. The stamp records the series
# of the last fetch: models rebuilt by hand from an edited tree keep it.
set -eu

SCRIPT_DIR=${MODELS_SERIES_DIR:-$(cd "$(dirname "$0")" && pwd)}

sha256() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum | cut -d' ' -f1
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 | cut -d' ' -f1
	else
		echo "models-series: no sha256sum or shasum in PATH" >&2
		return 2
	fi
}

series_id() {
	[ -r "$SCRIPT_DIR/fetch-renode-rp2040.sh" ] || {
		echo "models-series: no $SCRIPT_DIR/fetch-renode-rp2040.sh" >&2
		return 2
	}
	set -- "$SCRIPT_DIR"/patches/*.patch
	[ -e "$1" ] || {
		echo "models-series: no patches under $SCRIPT_DIR/patches" >&2
		return 2
	}
	{
		cat "$SCRIPT_DIR/fetch-renode-rp2040.sh"
		for patch in "$@"; do
			basename "$patch"
			cat "$patch"
		done
	} | sha256
}

check() {
	want=$(series_id) || return 2
	[ -n "$want" ] || return 2
	[ -r "$1.series" ] || {
		echo "models-series: $1 has no series stamp" >&2
		return 1
	}
	have=$(cat "$1.series")
	[ "$have" = "$want" ] || {
		echo "models-series: $1 was built from another patch series" >&2
		return 1
	}
}

selftest() {
	self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
	tmp=$(mktemp -d)
	trap 'rm -r "$tmp"' EXIT
	mkdir "$tmp/patches"
	echo "RP2040_COMMIT=0000" > "$tmp/fetch-renode-rp2040.sh"
	echo "first" > "$tmp/patches/0001-first.patch"
	echo "second" > "$tmp/patches/0002-second.patch"
	: > "$tmp/Peripherals.dll"
	expect() {
		want_status=$1
		label=$2
		shift 2
		status=0
		MODELS_SERIES_DIR=$tmp sh "$self" "$@" >/dev/null 2>&1 || status=$?
		[ "$status" -eq "$want_status" ] || {
			echo "models-series selftest: $label: status $status, expected $want_status" >&2
			exit 1
		}
	}
	expect 1 "a build without a stamp" check "$tmp/Peripherals.dll"
	expect 0 "stamp" stamp "$tmp/Peripherals.dll"
	expect 0 "a build of this series" check "$tmp/Peripherals.dll"
	echo "changed" >> "$tmp/patches/0002-second.patch"
	expect 1 "a patch edited after the build" check "$tmp/Peripherals.dll"
	echo "second" > "$tmp/patches/0002-second.patch"
	expect 0 "the patch restored" check "$tmp/Peripherals.dll"
	echo "third" > "$tmp/patches/0003-third.patch"
	expect 1 "a patch added after the build" check "$tmp/Peripherals.dll"
	rm "$tmp/patches/0003-third.patch"
	mv "$tmp/patches/0002-second.patch" "$tmp/patches/0002-renamed.patch"
	expect 1 "a patch renamed after the build" check "$tmp/Peripherals.dll"
	mv "$tmp/patches/0002-renamed.patch" "$tmp/patches/0002-second.patch"
	echo "RP2040_COMMIT=1111" > "$tmp/fetch-renode-rp2040.sh"
	expect 1 "the pin moved after the build" check "$tmp/Peripherals.dll"
	rm "$tmp"/patches/*.patch
	expect 2 "a series without patches" check "$tmp/Peripherals.dll"
	expect 2 "an unknown mode" frobnicate
	echo "models-series selftest: ok (stamp, match, edited, added and renamed patch, moved pin, empty series)"
}

case ${1:-} in
id)
	series_id
	;;
stamp)
	[ $# -eq 2 ] && [ -r "$2" ] || {
		echo "models-series: stamp needs the built models file" >&2
		exit 2
	}
	id=$(series_id) || exit 2
	[ -n "$id" ] || exit 2
	printf '%s\n' "$id" > "$2.series"
	;;
check)
	[ $# -eq 2 ] || {
		echo "models-series: check needs the built models file" >&2
		exit 2
	}
	check "$2"
	;;
selftest)
	selftest
	;;
*)
	echo "usage: models-series.sh id | stamp MODELS | check MODELS | selftest" >&2
	exit 2
	;;
esac
