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
# shares no code with it. Seven source mutants calibrate the cases: the old
# 0x7fffffff bound, a fixed clock that only caps the host time, a range
# check without an upper bound, a bad value that falls back to the host
# clock, a ceiling above 0x7fffffff that is ignored, an added file that
# keeps its host modification time, and --repartition without the value
# check. Each must fail the case named for it.
#
# Exit status: 0 when every case holds and every mutant is rejected, 1 on a
# failed case or an escaped mutant, 2 when the environment fails.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

libhsaout=${1:?usage: check.sh LIBHSAOUT}
topsrc=$(cd "$(dirname "$0")/../.." && pwd)
src=$topsrc/tools/fsutil
work=$(mktemp -d "${TMPDIR:-/tmp}/fsutil-clock.XXXXXX")

# The work directory holds plain files and the three flat subdirectories obj,
# dir and high, so each file is removed by name and each directory by rmdir.
cleanup()
{
	for sub in obj dir high; do
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
mkdir "$work/obj" "$work/dir" "$work/high"

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

units="fsutil inode create check file manifest block superblock"
for unit in $units; do
	compile "$src/$unit.c" "$work/obj/$unit.o"
done

# link_fsutil UNIT SOURCE BINARY: fsutil with SOURCE compiled in place of
# the translation unit UNIT.
link_fsutil()
{
	replaced_unit=$1
	binary=$3
	replaced=$work/obj/replaced_$(basename "$binary").o
	compile "$2" "$replaced"
	# The object paths hold TMPDIR, so they travel as positional parameters.
	set --
	for unit in $units; do
		if [ "$unit" = "$replaced_unit" ]; then
			set -- "$@" "$replaced"
		else
			set -- "$@" "$work/obj/$unit.o"
		fi
	done
	# shellcheck disable=SC2086  # the library list is a word list
	${CC:-cc} -o "$binary" "$@" "$libhsaout" $libbsd_libs ||
	    env_fail "link of $binary"
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

# A second host file, h, dated 3000000000 (after every ceiling the cases set
# and after 2038), alone in its own directory.
printf 'high\n' >"$work/high/h"
"$PYTHON" -c 'import os, sys
os.utime(sys.argv[1], (3000000000, 3000000000))' "$work/high/h"
sample_dir=$work/dir

# time_lines IMAGE: the time text of every line fsutil -vvv prints, one per line.
time_lines()
{
	env -u FSUTIL_CLOCK -u SOURCE_DATE_EPOCH TZ=UTC "$bin" -vvv "$1" |
	    sed -n -E 's/^ *(Last update time|Created|Modified|Last access): //p'
}

# file_times IMAGE NAME: the Created, Modified and Last access text of the
# file /NAME that fsutil -vvv prints, one per line.
file_times()
{
	env -u FSUTIL_CLOCK -u SOURCE_DATE_EPOCH TZ=UTC "$bin" -vvv "$1" |
	    sed -n "\\#^/$2 #,\\#^--------#p" |
	    sed -n -E 's/^ *(Created|Modified|Last access): //p'
}

# image_with IMAGE [VAR=VALUE...]: a new 512-kbyte image from sample_dir
# under the given environment; the status is fsutil's.
image_with()
{
	image=$1
	shift
	rm -f "$image"
	env -u FSUTIL_CLOCK -u SOURCE_DATE_EPOCH "$@" \
	    "$bin" -n -s 512 "$image" "$sample_dir" \
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

	# A ceiling above 2038 bounds a file dated past it: h, dated 3000000000,
	# takes the ceiling 2415919104 as its modification time.
	sample_dir=$work/high
	status=0
	image_with "$work/high.img" SOURCE_DATE_EPOCH=2415919104 || status=$?
	sample_dir=$work/dir
	if [ "$status" -ne 0 ]; then
		bad ceiling-above-2038 "SOURCE_DATE_EPOCH=2415919104 exited $status"
	elif [ "$(file_times "$work/high.img" h | sed -n 2p)" = "$(asctime_of 2415919104)" ]; then
		ok ceiling-above-2038
	else
		bad ceiling-above-2038 "the file dated past the ceiling kept its time"
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

	# --add stamps the added file, whose host modification time is
	# 3000000000, with the fixed clock in all three of its times, and
	# refuses a bad value before it opens the image. The base image holds
	# no h.
	image_with "$work/base.img" FSUTIL_CLOCK=1700000000 ||
	    env_fail "the base image for the --add case was not created"
	cp "$work/base.img" "$work/add.img"
	status=0
	(cd "$work/high" && env FSUTIL_CLOCK=2415919104 "$bin" --add \
	    "$work/add.img" h) >/dev/null 2>&1 || status=$?
	file_times "$work/add.img" h >"$work/times.txt"
	if [ "$status" -eq 0 ] && [ "$(wc -l <"$work/times.txt")" -eq 3 ] &&
	    [ "$(grep -c -x -F "$(asctime_of 2415919104)" "$work/times.txt")" -eq 3 ]; then
		ok add-uses-fixed-clock
	else
		bad add-uses-fixed-clock "exit $status, or a time of h is not the fixed clock"
	fi
	cp "$work/base.img" "$work/add.img"
	status=0
	(cd "$work/high" && env FSUTIL_CLOCK=4294967296 "$bin" --add \
	    "$work/add.img" h) >/dev/null 2>&1 || status=$?
	if [ "$status" -eq 1 ] && cmp -s "$work/base.img" "$work/add.img"; then
		ok reject-on-add
	else
		bad reject-on-add "exit $status or the image changed"
	fi

	# --repartition replaces or creates an image, so it refuses a bad value
	# first: an existing image stays byte-identical and none is created.
	# A good value creates one, which shows the refusal is the cause.
	cp "$work/base.img" "$work/part.img"
	status=0
	env FSUTIL_CLOCK=4294967296 "$bin" --repartition=fs=1m "$work/part.img" \
	    >/dev/null 2>&1 || status=$?
	if [ "$status" -eq 1 ] && cmp -s "$work/base.img" "$work/part.img"; then
		ok reject-on-repartition-existing
	else
		bad reject-on-repartition-existing "exit $status or the image changed"
	fi
	rm -f "$work/newpart.img"
	status=0
	env SOURCE_DATE_EPOCH=abc "$bin" --repartition=fs=1m "$work/newpart.img" \
	    >/dev/null 2>&1 || status=$?
	if [ "$status" -eq 1 ] && [ ! -e "$work/newpart.img" ]; then
		ok reject-on-repartition-missing
	else
		bad reject-on-repartition-missing "exit $status or an image was created"
	fi
	status=0
	env FSUTIL_CLOCK=2415919104 "$bin" --repartition=fs=1m "$work/newpart.img" \
	    >/dev/null 2>&1 || status=$?
	if [ "$status" -eq 0 ] && [ -e "$work/newpart.img" ]; then
		ok repartition-with-good-clock
	else
		bad repartition-with-good-clock "exit $status or no image was created"
	fi

	# A mode that writes no time reads an image whatever the variables hold.
	if env SOURCE_DATE_EPOCH=abc FSUTIL_CLOCK=abc "$bin" \
	    "$work/base.img" >/dev/null 2>&1; then
		ok read-ignores-clock
	else
		bad read-ignores-clock "a read exited nonzero on a bad variable"
	fi
}

# mutate UNIT NAME OLD NEW: a copy of UNIT.c with the one OLD replaced.
mutate()
{
	"$PYTHON" - "$src/$1.c" "$work/$2.c" "$3" "$4" <<'PY'
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

link_fsutil superblock "$src/superblock.c" "$work/fsutil"
cases "$work/fsutil" >"$work/fsutil.log"
if grep -q '^FAIL' "$work/fsutil.log"; then
	grep '^FAIL' "$work/fsutil.log" >&2
	echo "fsutil_clock: FAIL: fsutil does not hold the clock contract" >&2
	exit 1
fi
echo "fsutil_clock: $(grep -c '^ok' "$work/fsutil.log") cases hold"

mutate superblock old-bound '*seconds <= FS_TIME_MAX' '*seconds <= 0x7fffffffULL'
link_fsutil superblock "$work/old-bound.c" "$work/old-bound"
rejected old-bound fixed-past-2038 fixed-maximum add-uses-fixed-clock

mutate superblock cap-only 'return (time_t) fixed;' \
    'return observed > (time_t) fixed ? (time_t) fixed : observed;'
link_fsutil superblock "$work/cap-only.c" "$work/cap-only"
rejected cap-only fixed-past-2038 fixed-maximum

mutate superblock no-upper-bound '*seconds <= FS_TIME_MAX' '1'
link_fsutil superblock "$work/no-upper-bound.c" "$work/no-upper-bound"
rejected no-upper-bound 'reject-FSUTIL_CLOCK-\[4294967296\]' \
    'reject-SOURCE_DATE_EPOCH-\[4294967296\]'

mutate superblock fall-back '    exit (1);
}' '    return 0;
}'
link_fsutil superblock "$work/fall-back.c" "$work/fall-back"
rejected fall-back 'reject-FSUTIL_CLOCK-\[abc\]' reject-on-add

mutate superblock ceiling-low-only '(long long) observed > (long long) epoch)' \
    '(long long) epoch <= 0x7fffffffLL && (long long) observed > (long long) epoch)'
link_fsutil superblock "$work/ceiling-low-only.c" "$work/ceiling-low-only"
rejected ceiling-low-only ceiling-above-2038

mutate fsutil added-keeps-host-time 'file.inode.mtime = fs_source_time (st.st_mtime);' \
    'file.inode.mtime = st.st_mtime;'
link_fsutil fsutil "$work/added-keeps-host-time.c" "$work/added-keeps-host-time"
rejected added-keeps-host-time add-uses-fixed-clock

mutate fsutil no-repartition-check 'mount || repartition ||' 'mount ||'
link_fsutil fsutil "$work/no-repartition-check.c" "$work/no-repartition-check"
rejected no-repartition-check reject-on-repartition-existing \
    reject-on-repartition-missing

echo "fsutil_clock: every case holds and every mutant is rejected"
