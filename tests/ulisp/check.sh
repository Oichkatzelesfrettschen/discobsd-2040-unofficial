#!/bin/sh
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# usage: check.sh host|qemu DOCFILE COMMAND [ARG...]
#
# Runs the uLisp that COMMAND starts over the corpora of this directory, in
# a directory of its own, and compares each transcript with its .expected
# file. host runs the language, system, repairs and devices corpora; qemu
# runs the first three and, in place of devices, the stack and machine-code
# corpora that need the target. Both reload the image the system corpus
# saved in a second process with a smaller workspace, which rebases it.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
mode=$1
doc=$2
shift 2

work=$(mktemp -d "${TMPDIR:-/tmp}/ulisp-check.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"

status=0
# run NAME CELLS [OPTION...]: one corpus, compared with NAME.expected
run() {
	name=$1
	cells=$2
	shift 2
	if ! env -i ULISPLIB="$here/library.lisp" ULISPDOC="$doc" \
	    "$@" -w "$cells" < "$here/$name.lisp" > "$name.out" 2>&1; then
		echo "ulisp $mode: $name: the interpreter failed" >&2
		status=1
	fi
}
compare() {
	if ! diff "$here/$1.expected" "$1.out" > "$1.diff"; then
		echo "ulisp $mode: $1 differs from $1.expected:" >&2
		head -20 "$1.diff" >&2
		status=1
	fi
}

case $mode in
host)	corpora="language system repairs devices" ;;
qemu)	corpora="language system repairs stack code" ;;
*)	echo "usage: check.sh host|qemu DOCFILE COMMAND [ARG...]" >&2; exit 2 ;;
esac

for corpus in $corpora; do
	run "$corpus" 12000 "$@"
	if [ "$corpus" = devices ]; then
		cksum < ulisp-test.bmp >> devices.out
	fi
	compare "$corpus"
done
run reload 9000 "$@" -i ulisp-test.img
compare reload

if [ "$status" -eq 0 ]; then
	echo "ulisp $mode: $corpora reload: transcripts match"
fi
exit "$status"
