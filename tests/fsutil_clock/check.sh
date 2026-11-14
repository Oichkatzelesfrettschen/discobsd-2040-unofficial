#!/bin/sh
# Gate for the time stamps tools/fsutil writes into an image. The image
# stores every time as an unsigned 32-bit count of seconds since 1970, valid
# to 2106, so an image can carry a clock past January 2038 for a boot test.
#
#   check.sh LIBHSAOUT
#
# FSUTIL_CLOCK=N makes every time fsutil writes (superblock and inodes,
# including the modification time of an added host file) the count N,
# whatever SOURCE_DATE_EPOCH and the host clock hold. SOURCE_DATE_EPOCH
# keeps its meaning as a ceiling on the host time. Both variables accept
# 0 through 4294967295; any other value exits 1 with a message before an
# image is opened.
#
# The reader is fsutil itself (-vvv prints the superblock and inode times
# with ctime in UTC); the expected text comes from Python's asctime, which
# shares no code with it. Four source mutants calibrate the cases: the old
# 0x7fffffff bound, a fixed clock that only caps the host time, a range
# check without an upper bound, and a bad value that falls back to the host
# clock. Each must fail the case named for it.
#
# Exit status: 0 when every case holds and every mutant is rejected, 1 on a
# failed case or an escaped mutant, 2 when the environment fails.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

libhsaout=${1:?usage: check.sh LIBHSAOUT}
topsrc=$(cd "$(dirname "$0")/../.." && pwd)
src=$topsrc/tools/fsutil
work=$(mktemp -d "${TMPDIR:-/tmp}/fsutil-clock.XXXXXX")

# The work directory holds plain files and the two flat subdirectories obj
# and dir, so each file is removed by name and each directory by rmdir.
cleanup()
{
	for sub in obj dir; do
		for file in "$work/$sub"/*; do
			if [ -f "$file" ]; then rm -f "$file"; fi
		done
		rmdir "$work/$sub" 2>/dev/null || true
	done
	for file in "$work"/*; do
		if [ -f "$file" ]; then rm -f "$file"; fi
	done
	rmdir "$work" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
mkdir "$work/obj" "$work/dir"

env_fail()
{
	echo "fsutil_clock: ERROR: $*" >&2
	exit 2
}

# Host library flags, as tools/Makefile.inc selects them.
libbsd_cflags=
libbsd_libs=
if [ "$(uname -s)" = Linux ]; then
	libbsd_cflags=$(pkg-config libbsd-overlay --cflags) ||
	    env_fail "pkg-config libbsd-overlay (libbsd-dev) is required"
	libbsd_libs=$(pkg-config libbsd-overlay --libs) ||
	    env_fail "pkg-config libbsd-overlay (libbsd-dev) is required"
fi

# compile SOURCE OBJECT: one fsutil translation unit without FUSE.
compile()
{
	# shellcheck disable=SC2086  # the flag variable is a word list
	${CC:-cc} -std=gnu17 -O1 -g -Wall -Werror $libbsd_cflags \
	    -I"$src" -c -o "$2" "$1" || env_fail "build of $1"
}

units="fsutil inode create check file manifest block"
for unit in $units; do
	compile "$src/$unit.c" "$work/obj/$unit.o"
done

# link_fsutil SUPERBLOCK_SOURCE BINARY
link_fsutil()
{
	superblock=$work/obj/superblock_$(basename "$2").o
	compile "$1" "$superblock"
	objects=
	for unit in $units; do
		objects="$objects $work/obj/$unit.o"
	done
	# shellcheck disable=SC2086  # the object and library lists are word lists
	${CC:-cc} -o "$2" $objects "$superblock" "$libhsaout" $libbsd_libs ||
	    env_fail "link of $2"
}

asctime_of()
{
	"$PYTHON" -c 'import sys, time
print(time.asctime(time.gmtime(int(sys.argv[1]))))' "$1"
}

# A host file whose modification time is 2001-01-02 03:04 UTC, which lies
# below every ceiling the cases set.
printf 'clock\n' >"$work/dir/f"
TZ=UTC touch -t 200101020304 "$work/dir/f"
file_mtime=$(asctime_of 978404640)

# time_lines IMAGE: the time text of every line fsutil -vvv prints, one per line.
time_lines()
{
	env -u FSUTIL_CLOCK -u SOURCE_DATE_EPOCH TZ=UTC "$bin" -vvv "$1" |
	    sed -n -E 's/^ *(Last update time|Created|Modified|Last access): //p'
}

# image_with IMAGE [VAR=VALUE...]: a new 512-kbyte image from the sample
# directory under the given environment; the status is fsutil's.
image_with()
{
	image=$1
	shift
	rm -f "$image"
	env -u FSUTIL_CLOCK -u SOURCE_DATE_EPOCH "$@" \
	    "$bin" -n -s 512 "$image" "$work/dir" \
	    >"$work/create.out" 2>"$work/create.err"
}

# Case results are lines "ok LABEL" or "FAIL LABEL: reason" on stdout.
ok() { echo "ok $1"; }
bad() { echo "FAIL $1: $2"; }

# every_time_is LABEL IMAGE TEXT: each time line equals TEXT, with the
# superblock line and at least three inode lines present.
every_time_is()
{
	time_lines "$2" >"$work/times.txt"
	lines=$(wc -l <"$work/times.txt")
	differing=$(grep -c -v -x -F "$3" "$work/times.txt" || true)
	if [ "$lines" -ge 4 ] && [ "$differing" -eq 0 ]; then
		ok "$1"
	else
		bad "$1" "$differing of $lines times differ from $3"
	fi
}

# fixed_case LABEL COUNT [VAR=VALUE...]: FSUTIL_CLOCK=COUNT stamps every time.
fixed_case()
{
	label=$1
	count=$2
	shift 2
	status=0
	image_with "$work/$label.img" FSUTIL_CLOCK="$count" "$@" || status=$?
	if [ "$status" -ne 0 ]; then
		bad "$label" "fsutil exited $status on FSUTIL_CLOCK=$count"
	else
		every_time_is "$label" "$work/$label.img" "$(asctime_of "$count")"
	fi
}

# cases BINARY: every case against BINARY, results on stdout.
cases()
{
	bin=$1

	# Fixed clock past 2038 under a lower ceiling and with an older file,
	# then the largest and the smallest count.
	fixed_case fixed-past-2038 2415919104 SOURCE_DATE_EPOCH=1700000000
	fixed_case fixed-maximum 4294967295
	fixed_case fixed-zero 0

	# The ceiling still bounds host time: the superblock takes the ceiling
	# and the old file keeps its own earlier time.
	status=0
	image_with "$work/ceiling.img" SOURCE_DATE_EPOCH=1700000000 || status=$?
	if [ "$status" -ne 0 ]; then
		bad ceiling-bounds-host-time "fsutil exited $status"
	else
		time_lines "$work/ceiling.img" >"$work/times.txt"
		if [ "$(sed -n 1p "$work/times.txt")" = "$(asctime_of 1700000000)" ] &&
		    grep -q -x -F "$file_mtime" "$work/times.txt"; then
			ok ceiling-bounds-host-time
		else
			bad ceiling-bounds-host-time "superblock or file time wrong"
		fi
	fi

	# A ceiling above the host clock is accepted and raises nothing.
	status=0
	image_with "$work/high.img" SOURCE_DATE_EPOCH=2415919104 || status=$?
	if [ "$status" -ne 0 ]; then
		bad ceiling-not-a-clock "SOURCE_DATE_EPOCH=2415919104 exited $status"
	elif [ "$(time_lines "$work/high.img" | sed -n 1p)" = "$(asctime_of 2415919104)" ]; then
		bad ceiling-not-a-clock "the ceiling raised the host time"
	else
		ok ceiling-not-a-clock
	fi

	# Neither variable: the superblock time is the host clock.
	status=0
	image_with "$work/host.img" || status=$?
	if [ "$status" -ne 0 ]; then
		bad host-clock-default "fsutil exited $status"
	else
		stamped=$(time_lines "$work/host.img" | sed -n 1p)
		if "$PYTHON" -c 'import calendar, sys, time
t = calendar.timegm(time.strptime(sys.argv[1], "%a %b %d %H:%M:%S %Y"))
sys.exit(0 if abs(time.time() - t) < 600 else 1)' "$stamped"; then
			ok host-clock-default
		else
			bad host-clock-default "superblock time $stamped is not the host clock"
		fi
	fi

	# An empty SOURCE_DATE_EPOCH leaves the host time, as an absent one does.
	status=0
	image_with "$work/empty.img" SOURCE_DATE_EPOCH= || status=$?
	if [ "$status" -eq 0 ]; then
		ok empty-source-date-epoch
	else
		bad empty-source-date-epoch "an empty SOURCE_DATE_EPOCH exited $status"
	fi

	# Values outside 0 through 4294967295, or not plain decimal numbers,
	# exit 1 with a message naming the variable and leave no image.
	for value in 4294967296 abc "" -1 " 5" 0x10 5x 99999999999999999999999; do
		for variable in FSUTIL_CLOCK SOURCE_DATE_EPOCH; do
			if [ -z "$value" ] && [ "$variable" = SOURCE_DATE_EPOCH ]; then
				continue
			fi
			label="reject-$variable-[$value]"
			status=0
			image_with "$work/reject.img" "$variable=$value" || status=$?
			if [ "$status" -ne 1 ]; then
				bad "$label" "fsutil exited $status, not 1"
			elif ! grep -q "$variable" "$work/create.err"; then
				bad "$label" "no message names $variable"
			elif [ -e "$work/reject.img" ]; then
				bad "$label" "an image was created"
			else
				ok "$label"
			fi
		done
	done

	# --add stamps the added file with the fixed clock, and refuses a bad
	# value before it opens the image.
	image_with "$work/base.img" FSUTIL_CLOCK=1700000000 ||
	    env_fail "the base image for the --add case was not created"
	printf 'added\n' >"$work/dir/g"
	cp "$work/base.img" "$work/add.img"
	status=0
	(cd "$work/dir" && env FSUTIL_CLOCK=2415919104 "$bin" --add \
	    "$work/add.img" g) >/dev/null 2>&1 || status=$?
	if [ "$status" -eq 0 ] && ! cmp -s "$work/base.img" "$work/add.img" &&
	    [ "$(time_lines "$work/add.img" | sed -n 1p)" = "$(asctime_of 2415919104)" ]; then
		ok add-uses-fixed-clock
	else
		bad add-uses-fixed-clock "exit $status, or the image or its time is wrong"
	fi
	cp "$work/base.img" "$work/add.img"
	status=0
	(cd "$work/dir" && env FSUTIL_CLOCK=4294967296 "$bin" --add \
	    "$work/add.img" g) >/dev/null 2>&1 || status=$?
	if [ "$status" -eq 1 ] && cmp -s "$work/base.img" "$work/add.img"; then
		ok reject-on-add
	else
		bad reject-on-add "exit $status or the image changed"
	fi

	# A mode that writes no time reads an image whatever the variables hold.
	if env SOURCE_DATE_EPOCH=abc FSUTIL_CLOCK=abc "$bin" \
	    "$work/base.img" >/dev/null 2>&1; then
		ok read-ignores-clock
	else
		bad read-ignores-clock "a read exited nonzero on a bad variable"
	fi
}

# mutate NAME OLD NEW: a copy of superblock.c with the one OLD replaced.
mutate()
{
	"$PYTHON" - "$src/superblock.c" "$work/$1.c" "$2" "$3" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old, new = sys.argv[3], sys.argv[4]
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count {source.count(old)}: {old!r}")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
}

# rejected NAME LABEL...: the mutant binary fails every case LABEL names.
rejected()
{
	name=$1
	shift
	cases "$work/$name" >"$work/$name.log"
	for label in "$@"; do
		if ! grep -q "^FAIL $label" "$work/$name.log"; then
			cat "$work/$name.log" >&2
			echo "fsutil_clock: FAIL: mutant $name passed $label" >&2
			exit 1
		fi
	done
	echo "fsutil_clock: mutant $name rejected: $*"
}

link_fsutil "$src/superblock.c" "$work/fsutil"
cases "$work/fsutil" >"$work/fsutil.log"
if grep -q '^FAIL' "$work/fsutil.log"; then
	grep '^FAIL' "$work/fsutil.log" >&2
	echo "fsutil_clock: FAIL: fsutil does not hold the clock contract" >&2
	exit 1
fi
echo "fsutil_clock: $(grep -c '^ok' "$work/fsutil.log") cases hold"

mutate old-bound '*seconds <= FS_TIME_MAX' '*seconds <= 0x7fffffffULL'
link_fsutil "$work/old-bound.c" "$work/old-bound"
rejected old-bound fixed-past-2038 fixed-maximum add-uses-fixed-clock

mutate cap-only 'return (time_t) fixed;' \
    'return observed > (time_t) fixed ? (time_t) fixed : observed;'
link_fsutil "$work/cap-only.c" "$work/cap-only"
rejected cap-only fixed-past-2038 fixed-maximum

mutate no-upper-bound '*seconds <= FS_TIME_MAX' '1'
link_fsutil "$work/no-upper-bound.c" "$work/no-upper-bound"
rejected no-upper-bound 'reject-FSUTIL_CLOCK-\[4294967296\]' \
    'reject-SOURCE_DATE_EPOCH-\[4294967296\]'

mutate fall-back '    exit (1);
}' '    return 0;
}'
link_fsutil "$work/fall-back.c" "$work/fall-back"
rejected fall-back 'reject-FSUTIL_CLOCK-\[abc\]' reject-on-add

echo "fsutil_clock: every case holds and every mutant is rejected"
