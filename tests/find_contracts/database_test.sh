#!/bin/sh
# Behavioral contract for the locate database builders under usr.bin/find:
# bigram, which lists the character pairs updatedb ranks, and code, which
# front-compresses and bigram-encodes the sorted pathname list into
# /var/db/find.codes.
#
# Both read one pathname per line from standard input, and a record is one
# newline-terminated pathname of at most MAXPATH-1 bytes, which is the buffer
# find's reader reconstructs into. The cases drive that boundary from both
# sides: a pathname of exactly MAXPATH-1 bytes is a record, a longer one is
# refused by name rather than split into two records that name no file, and a
# final line ending at end of file without a newline is a record.
#
# The oracle for the encoder is find_codes_reference.py, which runs the
# documented format backwards, so a leaked newline, a split record or a
# mis-encoded count shows as a decoded list that differs from the input list.
set -eu

USAGE='usage: database_test.sh bigram code find_codes_reference.py srcdir'
BIGRAM=${1:?${USAGE}}
CODE=${2:?${USAGE}}
REFERENCE=${3:?${USAGE}}
SRCDIR=${4:?${USAGE}}

PYTHON=${PYTHON:-python3}

# The cases run from a scratch directory, so every argument becomes absolute
# first.
absolute() {
	case "$1" in
	/*) printf '%s\n' "$1" ;;
	*) printf '%s/%s\n' "$(pwd)" "$1" ;;
	esac
}
BIGRAM=$(absolute "${BIGRAM}")
CODE=$(absolute "${CODE}")
REFERENCE=$(absolute "${REFERENCE}")
SRCDIR=$(absolute "${SRCDIR}")

WORK=$(mktemp -d "${TMPDIR:-/tmp}/find_contracts.XXXXXX")
trap 'rm -rf "${WORK}"' EXIT INT TERM

OUT=${WORK}/out
ERR=${WORK}/err

checks=0
failures=0

check() {
	checks=$((checks + 1))
	if [ "$2" != "$3" ]; then
		failures=$((failures + 1))
		printf 'FAIL %s: want [%s], got [%s]\n' "$1" "$2" "$3" >&2
	fi
}

# Both programs read to end of file, so a case stalls only where a read stops
# consuming its input. Every run carries a watchdog, so such a regression fails
# by the case's name within BOUND seconds instead of hanging the tier.
# timeout(1) is neither POSIX nor present on macOS, so the bound is a
# background sleep and a kill.
BOUND=20
RC=0

run() {
	label=$1
	prog=$2
	input=$3
	shift 3
	rc=0
	"${prog}" "$@" < "${input}" > "${OUT}" 2> "${ERR}" &
	pid=$!
	( sleep "${BOUND}"; kill -9 "${pid}" ) >/dev/null 2>&1 &
	guard=$!
	wait "${pid}" || rc=$?
	kill "${guard}" 2>/dev/null || :
	wait "${guard}" 2>/dev/null || :
	if [ "${rc}" -gt 128 ]; then
		check "${label}: terminated by itself within ${BOUND}s" \
		    "exit status" "signal $((rc - 128))"
	fi
	RC=${rc}
}

# The bound the programs enforce comes from their own source, so the gate and
# the programs cannot drift apart.
maxpath_of() {
	sed -n 's/^#define[[:space:]]*MAXPATH[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$1"
}
MAXPATH=$(maxpath_of "${SRCDIR}/bigram.c")
check "bigram and code agree on MAXPATH" "${MAXPATH}" \
    "$(maxpath_of "${SRCDIR}/code.c")"
LIMIT=$((MAXPATH - 1))

# gets(3) stores an unbounded line into a fixed buffer. C11 removed it, and the
# process image here is one flat window shared by text, data, bss and stack
# with no guard page after a buffer, so a call in either builder is a defect
# whatever the input happens to be. The argument has to begin with an
# identifier or a quote for the match to count, which is what a call passes and
# what separates it from the manual-page reference a comment writes.
unbounded_reads() {
	grep -cE '(^|[^A-Za-z0-9_])gets[[:space:]]*\([[:space:]]*[A-Za-z_"]' "$1" || :
}
for src in bigram.c code.c find.c; do
	check "${src}: calls to gets(3)" 0 "$(unbounded_reads "${SRCDIR}/${src}")"
done

# A pathname of exactly the length asked for, built here rather than checked in.
path_of() {
	"${PYTHON}" -c 'import sys
sys.stdout.write("/" + "a" * (int(sys.argv[1]) - 1))' "$1"
}

cd "${WORK}"

# The sorted list updatedb feeds both programs. The last two entries descend
# far enough and return far enough that the differential count leaves the byte
# range twice, which is the escape code's only path.
cat > list <<'EOF'
/bin/cat
/bin/chmod
/bin/sh
/usr/bin/find
/usr/share/man/man1/find.1
/usr/share/man/man1/findx.1
/var/db/find.codes
EOF

# bigram lists the pairs, from the common prefix with the previous pathname on.
"${PYTHON}" "${REFERENCE}" bigram < list > want_bigrams
run "bigram: sorted list" "${BIGRAM}" list
check "bigram: exit status" 0 "${RC}"
check "bigram: pair stream" "$(cat want_bigrams)" "$(cat "${OUT}")"

# Each record is two bytes and a newline. A newline left inside a pathname
# would enter the stream as a pair of its own and move the count.
pairs=$(wc -l < "${OUT}" | tr -d ' ')
bytes=$(wc -c < "${OUT}" | tr -d ' ')
check "bigram: every record is two bytes and a newline" "$((pairs * 3))" \
    "${bytes}"

# The last line of a list a pipeline cut short is still a pathname.
printf '/bin/cat\n/bin/chmod' > nonewline
run "bigram: final line without a newline" "${BIGRAM}" nonewline
check "bigram: final line without a newline: exit status" 0 "${RC}"
"${PYTHON}" "${REFERENCE}" bigram < nonewline > want_nonewline
check "bigram: final line without a newline: pair stream" \
    "$(cat want_nonewline)" "$(cat "${OUT}")"

# Empty input is an empty database, not an error.
: > empty
run "bigram: empty input" "${BIGRAM}" empty
check "bigram: empty input: exit status" 0 "${RC}"
check "bigram: empty input: no output" 0 "$(wc -c < "${OUT}" | tr -d ' ')"

# A pathname of exactly MAXPATH-1 bytes is a record, so the bound rejects
# nothing the reader can hold.
path_of "${LIMIT}" > atlimit
printf '\n' >> atlimit
run "bigram: pathname at the bound" "${BIGRAM}" atlimit
check "bigram: pathname at the bound: exit status" 0 "${RC}"
"${PYTHON}" "${REFERENCE}" bigram < atlimit > want_atlimit
check "bigram: pathname at the bound: pair stream" "$(cat want_atlimit)" \
    "$(cat "${OUT}")"

# One byte past it is refused, by a message naming the bound, before any record
# of it reaches the output.
path_of "$((LIMIT + 1))" > overlimit
printf '\n' >> overlimit
run "bigram: pathname past the bound" "${BIGRAM}" overlimit
check "bigram: pathname past the bound: exit status" 1 "${RC}"
check "bigram: pathname past the bound: no output" 0 \
    "$(wc -c < "${OUT}" | tr -d ' ')"
check "bigram: pathname past the bound: names the bound" yes \
    "$(grep -q "${LIMIT}" "${ERR}" && echo yes || echo no)"

# The records before the long one stand, and none after it are invented.
printf '/bin/cat\n/bin/chmod\n' > prefix_list
cat prefix_list overlimit > mixed
printf '/bin/sh\n' >> mixed
run "bigram: long pathname among short ones" "${BIGRAM}" mixed
check "bigram: long pathname among short ones: exit status" 1 "${RC}"
"${PYTHON}" "${REFERENCE}" bigram < prefix_list > want_prefix
check "bigram: long pathname among short ones: records before it stand" \
    "$(cat want_prefix)" "$(cat "${OUT}")"

# The table updatedb builds: bigram's output ranked, the top 128 pairs, with
# the newlines stripped.
"${BIGRAM}" < list | sort | uniq -c | sort -nr | \
    awk '{ if (NR <= 128) print $2 }' | tr -d '\012' > bigrams
check "table: nonempty" yes \
    "$([ -s bigrams ] && echo yes || echo no)"

# The database the encoder writes decodes back to the list it was given.
run "code: sorted list" "${CODE}" list bigrams
check "code: exit status" 0 "${RC}"
cp "${OUT}" codes
"${PYTHON}" "${REFERENCE}" decode < codes > decoded
check "code: the database decodes to the list" "$(cat list)" "$(cat decoded)"

# A count outside the byte range rides the escape code, which the last two
# entries of the list reach.
resets=$(od -An -v -tu1 < codes | tr -s ' ' '\n' | grep -c '^30$' || :)
check "code: the escape code carries a count outside the byte range" yes \
    "$([ "${resets}" -ge 1 ] && echo yes || echo no)"

# The header is the table, padded to 256 bytes whatever the table holds, so a
# tree with fewer than 128 distinct bigrams still writes a database the reader
# can index.
"${PYTHON}" -c 'import sys
data = open(sys.argv[1], "rb").read()[:256]
sys.stdout.buffer.write(data + b"\0" * (256 - len(data)))' bigrams > want_header
dd if=codes of=got_header bs=256 count=1 2>/dev/null
check "code: the header is the padded table" yes \
    "$(cmp -s want_header got_header && echo yes || echo no)"

# A byte the reader cannot decode is squelched to a question mark, and the
# pathname keeps its length.
printf '/bin/ca\tt\n' > control
run "code: control byte in a pathname" "${CODE}" control bigrams
check "code: control byte in a pathname: exit status" 0 "${RC}"
"${PYTHON}" "${REFERENCE}" decode < "${OUT}" > decoded_control
check "code: control byte in a pathname: decodes as a question mark" \
    '/bin/ca?t' "$(cat decoded_control)"

# The last line of a list a pipeline cut short is a record here too.
run "code: final line without a newline" "${CODE}" nonewline bigrams
check "code: final line without a newline: exit status" 0 "${RC}"
"${PYTHON}" "${REFERENCE}" decode < "${OUT}" > decoded_nonewline
check "code: final line without a newline: decodes to both pathnames" \
    "$(printf '/bin/cat\n/bin/chmod')" "$(cat decoded_nonewline)"

# The bound, from both sides, in the encoder.
run "code: pathname at the bound" "${CODE}" atlimit bigrams
check "code: pathname at the bound: exit status" 0 "${RC}"
"${PYTHON}" "${REFERENCE}" decode < "${OUT}" > decoded_atlimit
check "code: pathname at the bound: decodes unchanged" \
    "$(cat atlimit)" "$(cat decoded_atlimit)"

run "code: pathname past the bound" "${CODE}" overlimit bigrams
check "code: pathname past the bound: exit status" 1 "${RC}"
check "code: pathname past the bound: only the header is written" 256 \
    "$(wc -c < "${OUT}" | tr -d ' ')"
check "code: pathname past the bound: names the bound" yes \
    "$(grep -q "${LIMIT}" "${ERR}" && echo yes || echo no)"

# Standard output carries the database, so a usage error goes to standard error
# and writes no database at all.
run "code: no operand" "${CODE}" list
check "code: no operand: exit status" 1 "${RC}"
check "code: no operand: no database" 0 "$(wc -c < "${OUT}" | tr -d ' ')"
check "code: no operand: reports on standard error" yes \
    "$([ -s "${ERR}" ] && echo yes || echo no)"

run "code: unreadable table" "${CODE}" list no_such_table
check "code: unreadable table: exit status" 1 "${RC}"
check "code: unreadable table: no database" 0 \
    "$(wc -c < "${OUT}" | tr -d ' ')"

if [ "${failures}" -ne 0 ]; then
	printf 'find contracts: %d of %d checks failed\n' \
	    "${failures}" "${checks}" >&2
	exit 1
fi
printf 'find contracts: %d checks passed\n' "${checks}" >&2
