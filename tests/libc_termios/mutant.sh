#!/bin/sh
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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
# One mutant of the termios library: apply a sed script to one source file
# of the tree, rebuild the gate against the mutated copy and require that the
# gate fails. A mutation that leaves the file unchanged, or that stops the gate from
# compiling, is itself an error, so a mutant counts only when it builds and
# a check rejects it.
#
# usage: mutant.sh LABEL FILE SED-SCRIPT
# environment: HOSTCC TOPSRC BUILD_FLAGS (compile flags, tree include path
# and renames, as the Makefile passes them to the real build)
set -eu

label=$1
file=$2
script=$3
: "${HOSTCC:?}" "${TOPSRC:?}" "${BUILD_FLAGS:?}"
members="${MEMBERS:?}"

work=$(mktemp -d "${TMPDIR:-/tmp}/libc_termios_mutant.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$work/inc" "$work/src"

sed -e "$script" "$TOPSRC/$file" > "$work/mutated"
if cmp -s "$work/mutated" "$TOPSRC/$file"; then
	echo "FAIL mutant $label: the script changed nothing in $file" >&2
	exit 1
fi

include_flags=
member_dir=$TOPSRC/lib/libc/gen
case $file in
include/termios.h)
	cp "$work/mutated" "$work/inc/termios.h"
	include_flags="-I$work/inc"
	;;
lib/libc/gen/*)
	;;
*)
	echo "FAIL mutant $label: $file is not a termios source" >&2
	exit 1
	;;
esac

objects=
for member in $members; do
	name=${member%.o}
	source=$member_dir/$name.c
	if [ "lib/libc/gen/$name.c" = "$file" ]; then
		cp "$work/mutated" "$work/src/$name.c"
		source=$work/src/$name.c
	fi
	# shellcheck disable=SC2086
	if ! $HOSTCC $include_flags $BUILD_FLAGS -c -o "$work/$name.o" "$source" \
	    2> "$work/compile.log"; then
		cat "$work/compile.log" >&2
		echo "FAIL mutant $label: the mutated source does not compile" >&2
		exit 1
	fi
	objects="$objects $work/$name.o"
done
# shellcheck disable=SC2086
if ! $HOSTCC $include_flags $BUILD_FLAGS -c -o "$work/gate.o" libc_termios_test.c \
    2> "$work/compile.log"; then
	cat "$work/compile.log" >&2
	echo "FAIL mutant $label: the gate does not compile against it" >&2
	exit 1
fi
for extra in ${EXTRA_OBJECTS:-}; do
	objects="$objects $extra"
done
# shellcheck disable=SC2086
$HOSTCC -m32 -o "$work/gate" "$work/gate.o" $objects
rc=0
sh "$(dirname "$0")/../../tools/run_bounded.sh" "$work/run.log" "$work/gate" || rc=$?
case $rc in
124 | 125)
	echo "FAIL mutant $label: the gate gave no verdict (status $rc: 124 the MUTANT_TIMEOUT bound, 125 the run could not happen)" >&2
	exit 3
	;;
0)
	echo "FAIL mutant $label survived: the gate passed" >&2
	exit 1
	;;
esac
echo "PASS mutant $label rejected"
