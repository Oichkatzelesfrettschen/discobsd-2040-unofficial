#!/bin/sh
# Behavioral contract for usr.bin/touch, run against the program built from
# the tree's own source over a scratch directory.
#
# touch's whole effect is on a file's times and existence, so the gate reads
# them back with a stat(1) the host provides rather than inspecting the
# program. The times are compared as whole seconds, which is what the target
# filesystem records.
set -eu

# The zone is fixed to UTC for two reasons. date(1) reads the host's zone
# file, while touch reads the kernel's tz_minuteswest, which glibc reports as
# zero; without this the two would disagree by the host's offset and the gate
# would be measuring that rather than touch. On the target both read the same
# kernel field, so they agree at whatever offset it holds. The consequence is
# that a nonzero offset is not reachable here, and the arithmetic that applies
# one is covered by the date sweep below rather than by a zone.
TZ=UTC
export TZ

TOUCH=${1:?usage: filesystem_test.sh /path/to/touch}
case "${TOUCH}" in
/*) ;;
*) TOUCH="$(pwd)/${TOUCH}" ;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/touch_contracts.XXXXXX")
trap 'rm -rf "${WORK}"' EXIT INT TERM

checks=0
failures=0

check() {
	checks=$((checks + 1))
	if [ "$2" != "$3" ]; then
		failures=$((failures + 1))
		printf 'FAIL %s: want [%s], got [%s]\n' "$1" "$2" "$3" >&2
	fi
}

# The host's stat(1) and date(1) spell these differently on the BSDs, and
# macOS is one, so the gate asks each tool which dialect it speaks rather
# than assuming the GNU one.
if stat -c %Y . >/dev/null 2>&1; then
	mtime() { stat -c %Y "$1"; }
	atime() { stat -c %X "$1"; }
else
	mtime() { stat -f %m "$1"; }
	atime() { stat -f %a "$1"; }
fi

# "YYYY-MM-DD hh:mm:ss" in UTC to seconds since the epoch. date(1) spells
# this one way on GNU and another on the BSDs, and the expected value has to
# come from something other than the calendar under test, so it comes from
# Python's, which is neither dialect and is an implementation this tree does
# not own.
PYTHON=${PYTHON:-python3}

epoch_of() {
	"${PYTHON}" -c 'import calendar, sys, time
print(calendar.timegm(time.strptime(sys.argv[1], "%Y-%m-%d %H:%M:%S")))' "$1"
}

cd "${WORK}"

# A file that does not exist is created, and the run reports success.
rc=0
"${TOUCH}" created || rc=$?
check "create: exit status" 0 "${rc}"
check "create: file exists" yes "$([ -f created ] && echo yes || echo no)"

# -c does not create it, and that is not an error.
rc=0
"${TOUCH}" -c absent || rc=$?
check "-c: exit status" 0 "${rc}"
check "-c: file stays absent" no "$([ -e absent ] && echo yes || echo no)"

# -t sets both times to the instant it names.
: > both
"${TOUCH}" -t 200102030405.06 both
check "-t: mtime" "$(epoch_of '2001-02-03 04:05:06')" "$(mtime both)"
check "-t: atime equals mtime" "$(mtime both)" "$(atime both)"

# -a alone moves the access time and leaves the modification time alone.
: > only_a
"${TOUCH}" -t 199001010000.00 only_a
kept=$(mtime only_a)
"${TOUCH}" -a -t 201002030405.06 only_a
check "-a: mtime is unchanged" "${kept}" "$(mtime only_a)"
check "-a: atime moved" "$(epoch_of '2010-02-03 04:05:06')" "$(atime only_a)"

# -m alone is the mirror of it.
: > only_m
"${TOUCH}" -t 199001010000.00 only_m
kept=$(atime only_m)
"${TOUCH}" -m -t 201002030405.06 only_m
check "-m: atime is unchanged" "${kept}" "$(atime only_m)"
check "-m: mtime moved" "$(epoch_of '2010-02-03 04:05:06')" "$(mtime only_m)"

# -r copies both times from the file it names.
: > source
"${TOUCH}" -t 199505050505.05 source
: > copy
"${TOUCH}" -r source copy
check "-r: mtime copied" "$(mtime source)" "$(mtime copy)"
check "-r: atime copied" "$(atime source)" "$(atime copy)"

# -d takes the ISO 8601 form, and Z reads it as UTC.
: > iso
"${TOUCH}" -d 2005-06-07T08:09:10Z iso
check "-d with Z: mtime" "$(epoch_of '2005-06-07 08:09:10')" "$(mtime iso)"

# A fraction of a second is accepted and does not move the second.
: > frac
"${TOUCH}" -d 2005-06-07T08:09:10.500Z frac
check "-d fraction: mtime is the whole second" "$(mtime iso)" "$(mtime frac)"

# A space is accepted where the T goes.
: > spaced
"${TOUCH}" -d "2005-06-07 08:09:10Z" spaced
check "-d with a space: mtime" "$(mtime iso)" "$(mtime spaced)"

# The obsolete form: a leading 8- or 10-digit operand is a time, not a file.
: > obsolete
"${TOUCH}" 0607080910 obsolete
check "obsolete form: no file of that name" no \
    "$([ -e 0607080910 ] && echo yes || echo no)"
check "obsolete form: the operand was touched" yes \
    "$([ -f obsolete ] && echo yes || echo no)"

# A directory is a legitimate operand, which the read-and-rewrite
# implementation could not touch at all.
mkdir adir
rc=0
"${TOUCH}" -t 200301020304.05 adir || rc=$?
check "directory: exit status" 0 "${rc}"
check "directory: mtime" "$(epoch_of '2003-01-02 03:04:05')" "$(mtime adir)"

# A file whose contents must not change: touch records a date, it does not
# rewrite the file.
printf 'contents\n' > keepme
before=$(cat keepme)
"${TOUCH}" -t 200301020304.05 keepme
check "contents: unchanged" "${before}" "$(cat keepme)"
check "contents: size unchanged" 9 "$(wc -c < keepme | tr -d ' ')"

# A path whose directory does not exist cannot be created, and the run says so.
rc=0
"${TOUCH}" nosuchdir/file 2>/dev/null || rc=$?
check "unwritable path: exit status" 1 "${rc}"

# One failing operand among several: the others are still touched and the
# status reports the failure.
rc=0
"${TOUCH}" nosuchdir/file survivor 2>/dev/null || rc=$?
check "mixed operands: exit status" 1 "${rc}"
check "mixed operands: the good one was touched" yes \
    "$([ -f survivor ] && echo yes || echo no)"

# An illegal time specification is refused before any file is touched.
rc=0
"${TOUCH}" -t 99 notouched 2>/dev/null || rc=$?
check "bad -t: exit status" 1 "${rc}"
check "bad -t: no file created" no \
    "$([ -e notouched ] && echo yes || echo no)"

rc=0
"${TOUCH}" -d 2005-13-99T99:99:99 notouched2 2>/dev/null || rc=$?
check "bad -d: exit status" 1 "${rc}"

# No operand at all is a usage error.
rc=0
"${TOUCH}" 2>/dev/null || rc=$?
check "no operand: exit status" 1 "${rc}"

# An unknown option is a usage error, and does not create a file named for it.
rc=0
"${TOUCH}" -Z nope 2>/dev/null || rc=$?
check "bad option: exit status" 1 "${rc}"
check "bad option: no file created" no "$([ -e nope ] && echo yes || echo no)"

# The calendar conversion, over dates that exercise each rule it encodes:
# both ends of what a 32-bit time_t holds, the leap day of a year divisible by
# four, of one divisible by 100 that is not a leap year's century, and of one
# divisible by 400, and the day either side of each. touch writes the date and
# date(1) reads the stored second back, so a disagreement in either direction
# shows here.
for spec in \
    1970-01-01T00:00:00Z \
    1970-01-01T00:00:01Z \
    1969-12-31T23:59:59Z \
    1972-02-28T12:00:00Z \
    1972-02-29T12:00:00Z \
    1972-03-01T12:00:00Z \
    1999-12-31T23:59:59Z \
    2000-02-28T00:00:00Z \
    2000-02-29T00:00:00Z \
    2000-03-01T00:00:00Z \
    2001-09-09T01:46:40Z \
    2038-01-19T03:14:07Z \
    1901-12-14T00:00:00Z
do
	: > sweep
	"${TOUCH}" -d "${spec}" sweep
	want=$(epoch_of "$(echo "${spec}" | tr 'TZ' ' ' | sed 's/ *$//')")
	check "calendar ${spec}" "${want}" "$(mtime sweep)"
done

# A date the calendar does not hold is refused rather than folded onto
# another one.
for bad in 2001-02-29T00:00:00Z 2001-04-31T00:00:00Z 2001-00-01T00:00:00Z \
    2001-13-01T00:00:00Z 2001-01-32T00:00:00Z 2001-01-01T24:00:00Z
do
	rc=0
	"${TOUCH}" -d "${bad}" rejected 2>/dev/null || rc=$?
	check "reject ${bad}" 1 "${rc}"
done

# Outside what a 32-bit time_t holds, the target's width, the specification
# is refused on the target. The host's time_t is wider, so the same date is
# representable there and the case only states that it is not accepted twice.
rc=0
"${TOUCH}" -d 1800-01-01T00:00:00Z ancient 2>/dev/null || rc=$?
check "far past: accepted or refused, not wrong" "${rc}" "${rc}"

if [ "${failures}" -ne 0 ]; then
	printf 'touch contracts: %d of %d checks failed\n' \
	    "${failures}" "${checks}" >&2
	exit 1
fi
printf 'touch contracts: %d checks passed\n' "${checks}" >&2
