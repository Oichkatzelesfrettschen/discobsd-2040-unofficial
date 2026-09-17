#!/bin/sh
# Host-side behavioral check for the sbase text tools imported under
# usr.bin (cut, paste, seq, dirname, nl, cksum, expand, unexpand,
# mkfifo, uuencode, uudecode). It builds each with the host cc against
# the same sources the rp2040 cross build uses -- only the alloc and
# rune shims a glibc host already carries (reallocarray, memmem,
# getline) are swapped for glibc's own, via ereallocarray_host.c --
# and diffs each tool's output against the matching GNU coreutils or
# sharutils program for the same input. The rp2040 a.out itself needs
# the board (see STORAGE.md); this only proves the shared C logic.
set -eu

PYTHON=${PYTHON:-python3}
CC=${CC:-cc}

# The references are GNU's. On macOS Homebrew installs coreutils under
# g-prefixed names and keeps the unprefixed set in libexec/gnubin, so that
# directory goes ahead on PATH where it exists.
if command -v brew >/dev/null 2>&1; then
	gnubin=$(brew --prefix coreutils 2>/dev/null)/libexec/gnubin
	[ -d "$gnubin" ] && PATH=$gnubin:$PATH
fi
HERE=$(cd "$(dirname "$0")" && pwd)
TB="$HERE/.."
UB="$TB/.."
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

CFLAGS="-D_DEFAULT_SOURCE -D_GNU_SOURCE -Wall -Wextra -Werror -I$TB -o"

# glibc carries reallocarray(3), so the host build takes only the
# ereallocarray wrapper and leaves the libc copy; macOS's libc has none,
# and the tree's reallocarray.c supplies both there.
case $(uname -s) in
Darwin)	EREALLOCARRAY=reallocarray.c ;;
*)	EREALLOCARRAY=tests/ereallocarray_host.c ;;
esac
fail=0

# build name tool.c compat1.c compat2.c ...
# tool.c is looked up as usr.bin/<name>/<name>.c; every other argument
# is looked up in usr.bin/textbox/ (compat helpers) or, prefixed
# "tests/", in usr.bin/textbox/tests/ (host-only shims).
build() {
	name=$1; shift
	srcs="$UB/$name/$name.c"
	shift
	for f in "$@"; do srcs="$srcs $TB/$f"; done
	if ! $CC $CFLAGS "$WORK/$name" $srcs 2>"$WORK/$name.cc.log"; then
		echo "BUILD FAIL: $name"
		cat "$WORK/$name.cc.log"
		fail=1
		return 1
	fi
	if [ -s "$WORK/$name.cc.log" ]; then
		echo "BUILD WARNINGS: $name"
		cat "$WORK/$name.cc.log"
		fail=1
	fi
	return 0
}

check() {
	desc=$1; got=$2; want=$3
	if ! cmp -s "$got" "$want"; then
		echo "FAIL: $desc"
		diff -u "$want" "$got" | head -20 || true
		fail=1
	else
		echo "ok: $desc"
	fi
}

# --- cut ---
build cut cut.c eprintf.c fshut.c unescape.c rune.c \
	$EREALLOCARRAY
printf 'a:b:c\nd:e:f\n' > "$WORK/cut.in"
"$WORK/cut" -d: -f2 "$WORK/cut.in" > "$WORK/cut.got"
cut -d: -f2 "$WORK/cut.in" > "$WORK/cut.want"
check "cut -d: -f2" "$WORK/cut.got" "$WORK/cut.want"
"$WORK/cut" -c1-3 "$WORK/cut.in" > "$WORK/cut.got2"
cut -c1-3 "$WORK/cut.in" > "$WORK/cut.want2"
check "cut -c1-3" "$WORK/cut.got2" "$WORK/cut.want2"

# --- paste ---
build paste paste.c eprintf.c fshut.c unescape.c rune.c \
	$EREALLOCARRAY
printf '1\n2\n3\n' > "$WORK/paste.a"
printf 'x\ny\nz\n' > "$WORK/paste.b"
"$WORK/paste" -d, "$WORK/paste.a" "$WORK/paste.b" > "$WORK/paste.got"
paste -d, "$WORK/paste.a" "$WORK/paste.b" > "$WORK/paste.want"
check "paste -d," "$WORK/paste.got" "$WORK/paste.want"

# --- seq (integer-only; GNU seq's float path is out of scope here) ---
build seq seq.c eprintf.c fshut.c strtonum.c
"$WORK/seq" 1 10 > "$WORK/seq.got"
seq 1 10 > "$WORK/seq.want"
check "seq 1 10" "$WORK/seq.got" "$WORK/seq.want"
"$WORK/seq" 5 2 20 > "$WORK/seq.got2"
seq 5 2 20 > "$WORK/seq.want2"
check "seq 5 2 20" "$WORK/seq.got2" "$WORK/seq.want2"
"$WORK/seq" -s, 1 5 > "$WORK/seq.got3"
seq -s, 1 5 > "$WORK/seq.want3"
check "seq -s, 1 5" "$WORK/seq.got3" "$WORK/seq.want3"
"$WORK/seq" -w 8 12 > "$WORK/seq.got4"
seq -w 8 12 > "$WORK/seq.want4"
check "seq -w 8 12" "$WORK/seq.got4" "$WORK/seq.want4"

# --- dirname ---
build dirname dirname.c eprintf.c fshut.c
"$WORK/dirname" /a/b/c > "$WORK/dirname.got"
dirname /a/b/c > "$WORK/dirname.want"
check "dirname /a/b/c" "$WORK/dirname.got" "$WORK/dirname.want"

# --- nl ---
build nl nl.c eprintf.c ealloc.c fshut.c strtonum.c unescape.c rune.c
printf 'one\ntwo\n\nthree\n' > "$WORK/nl.in"
"$WORK/nl" "$WORK/nl.in" > "$WORK/nl.got"
nl "$WORK/nl.in" > "$WORK/nl.want"
check "nl" "$WORK/nl.got" "$WORK/nl.want"

# --- cksum ---
build cksum cksum.c eprintf.c fshut.c
head -c 4096 /dev/urandom > "$WORK/cksum.in"
"$WORK/cksum" "$WORK/cksum.in" | cut -d' ' -f1,2 > "$WORK/cksum.got"
cksum "$WORK/cksum.in" | cut -d' ' -f1,2 > "$WORK/cksum.want"
check "cksum" "$WORK/cksum.got" "$WORK/cksum.want"

# --- expand / unexpand ---
build expand expand.c eprintf.c ealloc.c fshut.c strtonum.c rune.c \
	$EREALLOCARRAY
printf 'a\tb\tc\n\td\n' > "$WORK/tabs.in"
"$WORK/expand" "$WORK/tabs.in" > "$WORK/expand.got"
expand "$WORK/tabs.in" > "$WORK/expand.want"
check "expand" "$WORK/expand.got" "$WORK/expand.want"

build unexpand unexpand.c eprintf.c ealloc.c fshut.c strtonum.c rune.c \
	$EREALLOCARRAY
"$WORK/expand" "$WORK/tabs.in" | "$WORK/unexpand" -a > "$WORK/unexpand.got"
expand "$WORK/tabs.in" | unexpand -a > "$WORK/unexpand.want"
check "unexpand -a" "$WORK/unexpand.got" "$WORK/unexpand.want"

# --- mkfifo (not shipped in the box; built and probed for completeness) ---
build mkfifo mkfifo.c eprintf.c mode.c
"$WORK/mkfifo" "$WORK/fifo.test"
if [ -p "$WORK/fifo.test" ]; then
	echo "ok: mkfifo creates a FIFO-typed node"
else
	echo "FAIL: mkfifo did not create a FIFO node"
	fail=1
fi
rm -f "$WORK/fifo.test"

# --- uuencode / uudecode round trip on a binary file ---
build uuencode uuencode.c eprintf.c fshut.c
build uudecode uudecode.c eprintf.c fshut.c mode.c
head -c 8192 /dev/urandom > "$WORK/bin.orig"
"$WORK/uuencode" "$WORK/bin.orig" bin.orig > "$WORK/bin.uu"
( cd "$WORK" && "$WORK/uudecode" -o bin.out < bin.uu )
check "uuencode/uudecode traditional round trip" "$WORK/bin.out" "$WORK/bin.orig"

"$WORK/uuencode" -m "$WORK/bin.orig" bin.orig > "$WORK/bin.b64.uu"
( cd "$WORK" && "$WORK/uudecode" -o bin.b64.out < bin.b64.uu )
check "uuencode -m/uudecode base64 round trip" "$WORK/bin.b64.out" "$WORK/bin.orig"

# Cross-check against sharutils' uuencode/uudecode where installed,
# both the traditional and the -m base64 encoding in both directions:
# uudecode is the highest-priority tool because the console is how
# binaries reach the board, so what matters most is that our uudecode
# reads what a host's uuencode -m actually writes, not just what our
# own uuencode writes.
if command -v uuencode >/dev/null 2>&1 && command -v uudecode >/dev/null 2>&1; then
	uuencode "$WORK/bin.orig" bin.orig > "$WORK/bin.sys.uu"
	check "uuencode output matches system uuencode" "$WORK/bin.uu" "$WORK/bin.sys.uu"
	( cd "$WORK" && uudecode -o bin.sys.out < bin.uu )
	check "system uudecode reads our uuencode output" "$WORK/bin.sys.out" "$WORK/bin.orig"

	uuencode -m "$WORK/bin.orig" bin.orig > "$WORK/bin.sys.b64.uu"
	check "uuencode -m output matches system uuencode -m" \
		"$WORK/bin.b64.uu" "$WORK/bin.sys.b64.uu"
	( cd "$WORK" && "$WORK/uudecode" -o bin.fromsys.out < bin.sys.b64.uu )
	check "our uudecode reads system uuencode -m output" \
		"$WORK/bin.fromsys.out" "$WORK/bin.orig"
	( cd "$WORK" && uudecode -o bin.sys.b64.out < bin.b64.uu )
	check "system uudecode reads our uuencode -m output" \
		"$WORK/bin.sys.b64.out" "$WORK/bin.orig"
fi

if [ "$fail" -eq 0 ]; then
	echo "ALL TESTS PASSED"
else
	echo "SOME TESTS FAILED"
fi
exit "$fail"
