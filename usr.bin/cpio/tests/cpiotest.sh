#!/bin/sh
#
# Round-trip cpio(1) through the odc format and cross-check both directions
# against the host cpio, so the format is interoperable rather than merely
# self-consistent.
#
# The tool under test is a host build of usr.bin/cpio/cpio.c: the rp2040
# a.out runs only on the board, and the format code is the same translation
# unit. Pass a different binary as the first argument to test one that exists.
#
set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cpiotest.XXXXXX")
trap 'rm -rf "$work"' EXIT INT HUP TERM

fail() {
	echo "cpiotest: FAIL: $*" >&2
	exit 1
}

if [ $# -ge 1 ]; then
	CPIO=$1
else
	CPIO=$work/cpio
	${CC:-cc} -D_DEFAULT_SOURCE -std=c17 -O1 -Wall -Wextra -Werror \
	    -Wpedantic -Wstrict-prototypes -Wold-style-definition \
	    -fno-omit-frame-pointer -fsanitize=address,undefined \
	    -o "$CPIO" "$srcdir/cpio.c" ||
	    fail "host build of cpio.c"
fi
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS
echo "cpiotest: tool under test: $CPIO"

# The reference is GNU cpio. Homebrew's formula is keg-only because macOS
# ships a cpio of its own (bsdcpio, which has no --no-absolute-filenames),
# so on that host the keg's binary is the reference when HOSTCPIO is unset.
if [ -z "${HOSTCPIO:-}" ] && command -v brew >/dev/null 2>&1 &&
    [ -x "$(brew --prefix cpio 2>/dev/null)/bin/cpio" ]; then
	HOSTCPIO=$(brew --prefix cpio)/bin/cpio
fi
HOSTCPIO=${HOSTCPIO:-cpio}
command -v "$HOSTCPIO" >/dev/null 2>&1 || fail "no host cpio named $HOSTCPIO"

cd "$work"
mkdir -p tree/sub
echo 'the quick brown fox' > tree/plain
: > tree/empty
printf 'abcde' > tree/odd			# not a block multiple
printf '%0999d' 7 > tree/odd2
chmod 0751 tree/plain

echo "cpiotest: write odc"
find tree | "$CPIO" -o > a.odc
head -c 6 a.odc | grep -q '^070707$' || fail "no odc magic"

echo "cpiotest: list"
"$CPIO" -it < a.odc > a.list
grep -q '^tree/odd2$' a.list || fail "tree/odd2 missing from the listing"

echo "cpiotest: extract by the tool under test"
mkdir -p x-self
(cd x-self && "$CPIO" -id < ../a.odc)
diff -r tree x-self/tree || fail "odc self round trip differs"

echo "cpiotest: archive read by the host cpio"
mkdir -p x-host
(cd x-host && "$HOSTCPIO" -id --no-absolute-filenames < ../a.odc) 2>/dev/null ||
    fail "host cpio rejected the archive"
diff -r tree x-host/tree || fail "host cpio extract differs"

echo "cpiotest: archive written by the host cpio read by the tool under test"
find tree | "$HOSTCPIO" -o -H odc > h.odc 2>/dev/null
mkdir -p x-back
(cd x-back && "$CPIO" -id < ../h.odc)
diff -r tree x-back/tree || fail "reading the host cpio archive differs"

make_single_file_archive() {
	archive_file=$1
	entry_name=$2
	entry_data=$3
	entry_mode=${4:-0100644}
	entry_name_size=$((${#entry_name} + 1))
	entry_data_size=${#entry_data}
	{
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 "$entry_mode" 0 0 1 0 0 "$entry_name_size" \
		    "$entry_data_size"
		printf '%s\000%s' "$entry_name" "$entry_data"
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 0 0 0 1 0 0 11 0
		printf 'TRAILER!!!\000'
	} > "$archive_file"
}

expect_rejected_archive() {
	archive_file=$1
	extraction_directory=$2
	label=$3
	if (cd "$extraction_directory" && "$CPIO" -id < "$archive_file" \
	    > stdout 2> stderr); then
		fail "$label archive reported success"
	fi
}

expect_rejected_listing() {
	archive_file=$1
	label=$2
	if "$CPIO" -it < "$archive_file" > listing 2> listing-error; then
		fail "$label listing reported success"
	fi
	[ ! -s listing ] || fail "$label listing emitted an unsafe name"
}

write_archive_header() {
	entry_name=$1
	entry_data=$2
	entry_mode=$3
	printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
	    0 0 "$entry_mode" 0 0 1 0 0 $((${#entry_name} + 1)) \
	    "${#entry_data}"
	printf '%s\000%s' "$entry_name" "$entry_data"
}

echo "cpiotest: reject extraction escapes and existing targets"
mkdir x-absolute
absolute_victim=$work/absolute-victim
make_single_file_archive "$work/absolute.odc" "$absolute_victim" replaced
expect_rejected_archive "$work/absolute.odc" "$work/x-absolute" absolute
[ ! -e "$absolute_victim" ] || fail "absolute archive escaped extraction root"

mkdir x-dotdot
dotdot_victim=$work/dotdot-victim
make_single_file_archive "$work/dotdot.odc" ../dotdot-victim replaced
expect_rejected_archive "$work/dotdot.odc" "$work/x-dotdot" dotdot
[ ! -e "$dotdot_victim" ] || fail "dotdot archive escaped extraction root"

mkdir -p x-prefix
ln -s .. x-prefix/link
prefix_victim=$work/prefix-victim
make_single_file_archive "$work/prefix.odc" link/prefix-victim replaced
expect_rejected_archive "$work/prefix.odc" "$work/x-prefix" symlink-prefix
[ ! -e "$prefix_victim" ] || fail "symlink prefix escaped extraction root"

mkdir x-final
printf '%s\n' preserved > final-victim
ln -s ../final-victim x-final/victim
make_single_file_archive "$work/final.odc" victim replaced
expect_rejected_archive "$work/final.odc" "$work/x-final" symlink-final
grep -q '^preserved$' final-victim || fail "final symlink target changed"

mkdir x-existing
printf '%s\n' preserved > x-existing/victim
make_single_file_archive "$work/existing.odc" victim replaced
expect_rejected_archive "$work/existing.odc" "$work/x-existing" existing-file
grep -q '^preserved$' x-existing/victim || fail "existing file changed"

mkdir x-empty-component
make_single_file_archive "$work/empty-component.odc" dir//victim replaced
expect_rejected_archive "$work/empty-component.odc" \
    "$work/x-empty-component" empty-component

mkdir x-dot-component
make_single_file_archive "$work/dot-component.odc" ./victim replaced
expect_rejected_archive "$work/dot-component.odc" \
    "$work/x-dot-component" dot-component

mkdir x-unsupported
make_single_file_archive "$work/unsupported.odc" victim target 0120777
expect_rejected_archive "$work/unsupported.odc" \
    "$work/x-unsupported" unsupported-type
[ ! -e x-unsupported/victim ] || fail "unsupported entry created a target"
expect_rejected_listing "$work/unsupported.odc" unsupported-type

mkdir x-directory-data
make_single_file_archive "$work/directory-data.odc" directory x 040755
expect_rejected_archive "$work/directory-data.odc" \
    "$work/x-directory-data" directory-data
expect_rejected_listing "$work/directory-data.odc" directory-data

mkdir x-special-mode
make_single_file_archive "$work/special-mode.odc" privileged '' 0106755
(cd x-special-mode && "$CPIO" -id < "$work/special-mode.odc")
[ ! -u x-special-mode/privileged ] || fail "archive retained setuid mode"
[ ! -g x-special-mode/privileged ] || fail "archive retained setgid mode"

cp "$work/existing.odc" "$work/invalid-octal.odc"
printf '8' | dd of="$work/invalid-octal.odc" bs=1 seek=59 conv=notrunc \
    2>/dev/null
mkdir x-invalid-octal
expect_rejected_archive "$work/invalid-octal.odc" \
    "$work/x-invalid-octal" invalid-octal

{
	printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
	    0 0 0100644 0 0 1 0 0 7 0
	printf 'victimX'
} > "$work/missing-terminator.odc"
mkdir x-missing-terminator
expect_rejected_archive "$work/missing-terminator.odc" \
    "$work/x-missing-terminator" missing-terminator

{
	printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
	    0 0 0100644 0 0 1 0 0 9 0
	printf 'bad\000tail\000'
} > "$work/embedded-terminator.odc"
mkdir x-embedded-terminator
expect_rejected_archive "$work/embedded-terminator.odc" \
    "$work/x-embedded-terminator" embedded-terminator

{
	printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
	    0 0 0100644 0 0 1 0 0 7 0
	printf 'victim\000'
} > "$work/missing-trailer.odc"
mkdir x-missing-trailer
expect_rejected_archive "$work/missing-trailer.odc" \
    "$work/x-missing-trailer" missing-trailer

echo "cpiotest: retain owner traversal for created directories"
{
	write_archive_header locked '' 040000
	write_archive_header locked/child x 0100600
	printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
	    0 0 0 0 0 1 0 0 11 0
	printf 'TRAILER!!!\000'
} > "$work/owner-traversal.odc"
mkdir x-owner-traversal
(cd x-owner-traversal && "$CPIO" -id < "$work/owner-traversal.odc")
[ "$(cat x-owner-traversal/locked/child)" = x ] ||
    fail "created directory blocked a later child"

newline_name=$(printf 'forged\nentry')
make_single_file_archive "$work/newline.odc" "$newline_name" replaced
expect_rejected_listing "$work/newline.odc" newline

escape_name=$(printf 'forged\033[2Jentry')
make_single_file_archive "$work/escape.odc" "$escape_name" replaced
expect_rejected_listing "$work/escape.odc" escape

echo "cpiotest: reject archive creation through symlinks"
printf '%s\n' preserved > archive-source
ln -s archive-source archive-link
if printf '%s\n' archive-link | "$CPIO" -o > symlink.odc 2> symlink-error; then
	fail "symlink archive input reported success"
fi
[ ! -s symlink.odc ] || fail "symlink archive input produced an archive"

if printf '%s\n' "$work/archive-source" | "$CPIO" -o \
    > absolute-output.odc 2> absolute-output-error; then
	fail "absolute archive input reported success"
fi
[ ! -s absolute-output.odc ] || fail "absolute input produced an archive"

if printf '%s\n' missing-input | "$CPIO" -o \
    > missing-output.odc 2> missing-output-error; then
	fail "missing archive input reported success"
fi
[ ! -s missing-output.odc ] || fail "missing input produced an archive"

echo "cpiotest: PASS"
