#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# Stage the tree a PicoCalc SD root is made from when the root carries more
# than the install tree: a copy of DESTDIR with each OVERLAY directory laid
# over it.
#
# Usage: sh tools/picocalc/stage-sdroot.sh DESTDIR STAGE OVERLAY...
#
# Each OVERLAY mirrors the root: OVERLAY/usr/share/doc/x lands at
# /usr/share/doc/x. STAGE is replaced. An overlay adds files and directories
# and never replaces one: a path that exists in the tree already, as a file
# of the install tree or of an earlier overlay, stops the run, and so does
# an overlay entry that is not a regular file or a directory (a symbolic
# link could name a path outside the overlay). distrib/rp2040/mksdmanifest.py
# then gives every added path owner 0, group 0 and mode 0664 or 0775, and
# tools/picocalc/mksdroot.sh builds the image from STAGE.

set -eu

if [ $# -lt 3 ]; then
	echo "usage: stage-sdroot.sh DESTDIR STAGE OVERLAY..." >&2
	exit 2
fi
DESTDIR=$1
STAGE=$2
shift 2
[ -d "$DESTDIR" ] || { echo "stage-sdroot: no tree at $DESTDIR" >&2; exit 2; }
case $STAGE in
/*) ;;
*) echo "stage-sdroot: STAGE must be an absolute path" >&2; exit 2 ;;
esac
for overlay in "$@"; do
	if [ -L "$overlay" ] || [ ! -d "$overlay" ]; then
		echo "stage-sdroot: overlay $overlay is not a directory" >&2
		exit 2
	fi
done

# The copy is made beside STAGE and takes its name once every overlay has
# been accepted, so a refused overlay leaves the previous STAGE in place.
NEW=$(mktemp -d "$STAGE.new.XXXXXXXX")
trap 'chmod -R u+w "$NEW" 2>/dev/null; rm -r "$NEW" 2>/dev/null || true' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cp -pR "$DESTDIR" "$NEW/tree"
chmod -R u+w "$NEW/tree"
for overlay in "$@"; do
	bad=$(cd "$overlay" && find . ! -type f ! -type d | head -n 1)
	if [ -n "$bad" ]; then
		echo "stage-sdroot: $overlay/$bad is not a regular file or a directory" >&2
		exit 1
	fi
	(cd "$overlay" && find . -type f) | while IFS= read -r path; do
		if [ -e "$NEW/tree/$path" ] || [ -L "$NEW/tree/$path" ]; then
			echo "stage-sdroot: $overlay/$path would replace a file of the root" >&2
			exit 1
		fi
	done
	(cd "$overlay" && find . -type d) | while IFS= read -r path; do
		if [ -L "$NEW/tree/$path" ] ||
		    { [ -e "$NEW/tree/$path" ] && [ ! -d "$NEW/tree/$path" ]; }; then
			echo "stage-sdroot: $overlay/$path is not a directory of the root" >&2
			exit 1
		fi
	done
	cp -pR "$overlay/." "$NEW/tree/"
done
if [ -e "$STAGE" ]; then
	chmod -R u+w "$STAGE"
	rm -r "$STAGE"
fi
mv "$NEW/tree" "$STAGE"
echo "stage-sdroot: $STAGE from $DESTDIR and $# overlay(s)"
