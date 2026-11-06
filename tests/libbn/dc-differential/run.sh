#!/bin/sh
# Differential run of OpenBSD dc linked against libbn and against the host
# libcrypto. Needs the OpenBSD dc sources (DC_SRC, the pinned commit in
# docs/research/openbsd-dc-bn-port.md) and a host libcrypto; it runs the
# downloaded dc, so it is an owner-approved manual check, not a CI gate.
#
# usage: [M32=yes] [SIDE_B=pinned|ported] run.sh DC_SRC [PROGRAMS]
#
# Side a is the pinned sources against libcrypto. Side b links lib/libbn:
# SIDE_B=pinned (the default) builds the pinned sources with the
# compatibility header in this directory; SIDE_B=ported builds usr.bin/dc
# from this tree, whose own compat.h carries the same adaptations. M32=yes
# builds side b at -m32, so dc's u_long and size_t are 32 bits as on the
# target.
set -eu
src=$1
count=${2:-3000}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dc-differential.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
: "${PYTHON:?set PYTHON to the intended interpreter}"
mkdir "$work/a" "$work/b"
files="$src/main.c $src/dc.c $src/bcode.c $src/inout.c $src/mem.c $src/stack.c"
flags="-O1 -w -include $here/obsd_compat.h -I$src"
m32=
[ "${M32:-no}" = yes ] && m32=-m32
# shellcheck disable=SC2086
cc $flags -o "$work/a/dc" $files -lcrypto
case ${SIDE_B:-pinned} in
pinned)
	# shellcheck disable=SC2086
	cc $m32 $flags -I"$here/bnshim" -o "$work/b/dc" $files "$root/lib/libbn/bn.c"
	;;
ported)
	port="$root/usr.bin/dc"
	printf 'extern const char *__progname;\n' > "$work/port.h"
	printf 'int optreset;\n' > "$work/port.c"
	cc $m32 -std=c17 -pedantic-errors -Wall -Wextra -Werror -D_DEFAULT_SOURCE -O1 \
	    -include "$work/port.h" -I"$root/lib/libbn" -o "$work/b/dc" \
	    "$port/main.c" "$port/dc.c" "$port/bcode.c" "$port/inout.c" \
	    "$port/mem.c" "$port/stack.c" "$root/lib/libbn/bn.c" "$work/port.c"
	;;
*)
	echo "run.sh: SIDE_B must be pinned or ported" >&2
	exit 2
	;;
esac
"$PYTHON" -I "$here/gen_corpus.py" "$count" > "$work/corpus"
n=0
diffs=0
while IFS= read -r prog; do
	n=$((n + 1))
	printf '%s\n' "$prog" > "$work/in"
	ra=0; (cd "$work/a" && timeout 60 ./dc < "$work/in" > "$work/oa" 2>&1) || ra=$?
	rb=0; (cd "$work/b" && timeout 60 ./dc < "$work/in" > "$work/ob" 2>&1) || rb=$?
	if [ "$ra" != "$rb" ] || ! cmp -s "$work/oa" "$work/ob"; then
		diffs=$((diffs + 1))
		printf 'DIFF %d rc=%s/%s: %s\n' "$n" "$ra" "$rb" "$prog"
	fi
done < "$work/corpus"
echo "programs=$n diffs=$diffs"
