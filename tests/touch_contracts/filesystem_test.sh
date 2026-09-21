#!/bin/sh
# Behavioral contract for usr.bin/touch, run against the program built from
# the tree's own source over a scratch directory.
#
# touch's whole effect is on a file's times and existence, so the gate reads
# them back with a stat(1) the host provides rather than inspecting the
# program. The times are compared as whole seconds, which is what the target
# filesystem records.
set -eu

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

# The host's stat(1) spells its format differently on the BSDs.
if stat -c %Y . >/dev/null 2>&1; then
	mtime() { stat -c %Y "$1"; }
	atime() { stat -c %X "$1"; }
else
	mtime() { stat -f %m "$1"; }
	atime() { stat -f %a "$1"; }
fi

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
check "-t: mtime" "$(date -d '2001-02-03 04:05:06' +%s 2>/dev/null ||
    echo skip)" "$(mtime both)"
check "-t: atime equals mtime" "$(mtime both)" "$(atime both)"

# -a alone moves the access time and leaves the modification time alone.
: > only_a
"${TOUCH}" -t 199001010000.00 only_a
kept=$(mtime only_a)
"${TOUCH}" -a -t 201002030405.06 only_a
check "-a: mtime is unchanged" "${kept}" "$(mtime only_a)"
check "-a: atime moved" "$(date -d '2010-02-03 04:05:06' +%s 2>/dev/null ||
    echo skip)" "$(atime only_a)"

# -m alone is the mirror of it.
: > only_m
"${TOUCH}" -t 199001010000.00 only_m
kept=$(atime only_m)
"${TOUCH}" -m -t 201002030405.06 only_m
check "-m: atime is unchanged" "${kept}" "$(atime only_m)"
check "-m: mtime moved" "$(date -d '2010-02-03 04:05:06' +%s 2>/dev/null ||
    echo skip)" "$(mtime only_m)"

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
check "-d with Z: mtime" "$(date -u -d '2005-06-07 08:09:10' +%s 2>/dev/null ||
    echo skip)" "$(mtime iso)"

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
check "directory: mtime" "$(date -d '2003-01-02 03:04:05' +%s 2>/dev/null ||
    echo skip)" "$(mtime adir)"

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

if [ "${failures}" -ne 0 ]; then
	printf 'touch contracts: %d of %d checks failed\n' \
	    "${failures}" "${checks}" >&2
	exit 1
fi
printf 'touch contracts: %d checks passed\n' "${checks}" >&2
