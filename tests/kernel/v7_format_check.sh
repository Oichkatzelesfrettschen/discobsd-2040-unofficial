#!/bin/sh
# Gate for the V7 format descriptor (sys/sys/fsgeom.h, sys/kern/ufs_v7.c).
#
# Usage: v7_format_check.sh TOPSRC WORKDIR
#
# Three implementations must agree on one volume. The NetBSD core in
# usr.sbin/v7fs formats a volume and stores files; the kernel's inode
# layer, built for the host with V7FS by v7_format_test.c, reads them,
# then creates, truncates and removes files and directories; the core
# reads every surviving file back byte for byte, and v7_check.py, which
# shares code with neither, accounts for every block and link. Each
# mutant of the kernel sources must then fail that same run, and
# v7_check.py must reject each damaged image.
#
# A mutant must fail by verdict. One that leaves an inode locked spins in
# the host sleep stub, so each run of the test program has V7_RUN_SECONDS
# (default 120) and a run that outlives it fails the gate by name.
#
# WORKDIR must be absent or empty; the gate leaves its evidence there.
set -eu

top=$(cd "$1" && pwd)
work=$2
PYTHON=${PYTHON:?set PYTHON to the intended interpreter}
HOST_CC=${HOST_CC:-cc}
V7_RUN_SECONDS=${V7_RUN_SECONDS:-120}
here=$top/tests/kernel
tool=$top/usr.sbin/v7fs/v7fs-host

if [ -e "$work" ] && [ -n "$(ls -A "$work")" ]; then
	echo "v7_format_check.sh: $work is not empty" >&2
	exit 2
fi
mkdir -p "$work"
work=$(cd "$work" && pwd)

kflags="-std=gnu17 -ffreestanding -fno-builtin -DKERNEL -Dprintf=hk_kprintf
-Dfree=ufs_free -Wall -Wextra -Werror -Wno-deprecated-non-prototype -m32 -g
-fsanitize=undefined -fno-sanitize-recover=all -DV7FS -DNMOUNT=2
-DLINEAR_INODE_CACHE -DCOMPACT_INODE_FIELDS -DLINEAR_NAME_CACHE
-DLINEAR_BUFFER_CACHE"

# build SYSROOT OUT: compile the inode layer from SYSROOT (a sys/ tree) and
# link the gate into OUT.
build() {
	sys=$1
	out=$2
	mkdir -p "$out"
	inc="-I$here -I$sys -I$sys/arch"
	for f in ufs_alloc ufs_bmap ufs_inode ufs_v7 ufs_namei; do
		# shellcheck disable=SC2086 # flag lists
		$HOST_CC $kflags $inc -c -o "$out/$f.o" "$sys/kern/$f.c"
	done
	# shellcheck disable=SC2086
	$HOST_CC $kflags $inc -Dsetjmp=hk_setjmp -c -o "$out/sys_inode.o" \
	    "$sys/kern/sys_inode.c"
	# shellcheck disable=SC2086
	$HOST_CC $kflags $inc -c -o "$out/test.o" "$here/v7_format_test.c"
	$HOST_CC -std=c17 -Wall -Wextra -Werror -m32 -c -o "$out/host.o" \
	    "$here/v7_format_host.c"
	$HOST_CC -std=gnu17 -Wall -Wextra -Werror -m32 -c -o "$out/hostkern.o" \
	    "$here/hostkern.c"
	$HOST_CC -m32 -fsanitize=undefined -o "$out/v7_format_test" \
	    "$out"/*.o
}

# run BIN DIR: the whole agreement, in DIR. Returns 124 when the test
# program outlives V7_RUN_SECONDS and another nonzero status on any
# disagreement.
run() {
	bin=$1
	dir=$2
	mkdir -p "$dir/expect"
	(
		cd "$dir"
		status=0
		timeout "$V7_RUN_SECONDS" \
		    "$bin" "$work/volume.img" out.img expect \
		    /a="$work/a.bin" /b="$work/b.bin" /c="$work/c.bin" \
		    /dir/d="$work/d.bin" >test.log 2>&1 || status=$?
		[ "$status" -ne 124 ] || exit 124
		[ "$status" -eq 0 ] || exit 1
		for f in expect/*; do
			path=/$(basename "$f" | tr '%' '/')
			case $path in
			/doomed-double|/kdir/n00|/kdir/n05|/kdir/n39)
				! "$tool" get out.img "$path" gone.tmp \
				    >/dev/null 2>&1 || exit 1
				continue ;;
			esac
			"$tool" get out.img "$path" got.tmp >/dev/null 2>&1 ||
			    exit 1
			cmp -s got.tmp "$f" || exit 1
		done
		for pair in /a:a.bin /b:b.bin /c:c.bin /dir/d:d.bin; do
			"$tool" get out.img "${pair%%:*}" got.tmp >/dev/null
			cmp -s got.tmp "$work/${pair##*:}" || exit 1
		done
		"$PYTHON" "$here/v7_check.py" out.img >check.log 2>&1 || exit 1
	)
}

[ -x "$tool" ] || { echo "v7_format_check.sh: build $tool first" >&2; exit 2; }

# Fixed bytes, so the run is the same on every host.
"$PYTHON" - "$work" <<'PY'
import sys
state = 12345
def data(n):
    global state
    out = bytearray()
    for _ in range(n):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out.append(state >> 16 & 0xFF)
    return bytes(out)
for name, size in (("a", 100), ("b", 6000), ("c", 40000), ("d", 120000)):
    open(f"{sys.argv[1]}/{name}.bin", "wb").write(data(size))
PY
(
	cd "$work"
	"$tool" newfs volume.img 4096 >/dev/null
	"$tool" put volume.img a.bin /a
	"$tool" put volume.img b.bin /b
	"$tool" put volume.img c.bin /c
	"$tool" mkdir volume.img /dir
	"$tool" put volume.img d.bin /dir/d
) >/dev/null
"$PYTHON" "$here/v7_check.py" "$work/volume.img"
"$PYTHON" "$here/v7_check.py" --self-test "$work/volume.img"

[ -e "$here/machine/.stamp" ] || {
	echo "v7_format_check.sh: tests/kernel/machine is not prepared" >&2
	exit 2
}
build "$top/sys" "$work/build"
status=0
run "$work/build/v7_format_test" "$work/run" || status=$?
if [ "$status" -eq 124 ]; then
	echo "FAIL v7-format: the test program ran past $V7_RUN_SECONDS seconds" >&2
	exit 1
fi
if [ "$status" -ne 0 ]; then
	cat "$work/run/test.log" "$work/run/check.log" 2>/dev/null >&2 || true
	echo "FAIL v7-format: the three implementations disagree" >&2
	exit 1
fi
tail -n 2 "$work/run/test.log"
cat "$work/run/check.log"
echo "PASS v7-format: kernel reads the core's volume; the core and the checker accept the kernel's"

# Mutants: each breaks one claim of the design and must fail the run.
"$PYTHON" "$here/v7_format_mutate.py" "$top/sys" "$work/mutants"
for m in "$work"/mutants/*/; do
	name=$(basename "$m")
	if ! build "$m" "$work/mbuild-$name" >"$work/mbuild-$name.log" 2>&1; then
		echo "FAIL v7-format mutant $name does not compile" >&2
		exit 1
	fi
	status=0
	run "$work/mbuild-$name/v7_format_test" "$work/mrun-$name" || status=$?
	if [ "$status" -eq 0 ]; then
		echo "FAIL v7-format mutant $name passed" >&2
		exit 1
	fi
	if [ "$status" -eq 124 ]; then
		echo "FAIL v7-format mutant $name ran past $V7_RUN_SECONDS seconds" >&2
		exit 1
	fi
	echo "PASS v7-format mutant $name rejected"
done
