#!/bin/sh
#
# Round-trip tar(1) through both header formats it writes and cross-check
# the ustar archive against the host tar, so the format is interoperable
# rather than merely self-consistent.
#
# The tool under test is a host build of bin/tar/tar.c: the rp2040 a.out
# runs only on the board, and the format code is the same translation unit.
# Pass a different binary as the first argument to test one that exists.
#
set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tartest.XXXXXX")
trap 'rm -rf "$work"' EXIT INT HUP TERM

fail() {
	echo "tartest: FAIL: $*" >&2
	exit 1
}

#
# The LZW filter -z and -Z fork. On the board it is /usr/bin/compress, the
# tree's own usr.bin/compress; here it is whatever host program answers to
# the name, which reads and writes the same format.
#
ZPROG=$(command -v compress 2>/dev/null || true)

#
# The tool under test.
#
if [ $# -ge 1 ]; then
	TAR=$1
else
	TAR=$work/tar
	${CC:-cc} -D_DEFAULT_SOURCE -DTAR_PATH_LIMIT=256 -std=c17 -O1 \
	    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
	    -Wold-style-definition -Wconversion -Wsign-conversion \
	    -fno-omit-frame-pointer \
	    -fsanitize=address,undefined \
	    ${ZPROG:+-DCOMPRESS=\"$ZPROG\"} \
	    -o "$TAR" "$srcdir/tar.c" ||
	    fail "host build of tar.c"
fi
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS
echo "tartest: tool under test: $TAR"

#
# The host tar that proves the ustar archive is readable elsewhere. GNU tar
# and bsdtar both answer to this name; either one settles interoperability.
#
HOSTTAR=${HOSTTAR:-tar}
command -v "$HOSTTAR" >/dev/null 2>&1 || fail "no host tar named $HOSTTAR"
echo "tartest: host tar: $("$HOSTTAR" --version 2>&1 | head -1)"

#
# GNU tar writes its own format by default: magic "ustar  " rather than the
# POSIX "ustar\0", a LongName entry of typeflag 'L' rather than the prefix
# field, and time fields where ustar puts prefix. Ask for ustar explicitly
# when the host tar writes the archive. bsdtar spells the option the same.
#
"$HOSTTAR" --format=ustar --version >/dev/null 2>&1 &&
    HOSTFMT=--format=ustar || HOSTFMT=

#
# A tree exercising every case the header fields have to carry: a path
# needing the ustar prefix field, an empty file, a file whose size is not a
# multiple of TBLOCK, a symbolic link, and a hard link.
#
# The long path is nested directories rather than one long basename: ustar
# splits a path at a slash, so a single component over 99 bytes is
# unrepresentable in the format and a deep path is the case that works.
#
deep=d1234567890123456789012345678901234567890/d2234567890123456789012345678901234567890
deep=$deep/d3234567890123456789012345678901234567890/d4234567890123456789012345678901234567890
deep=$deep/d5234567890123456789012345678901234567890

mkshort() {
	root=$1
	mkdir -p "$root/sub"
	echo 'the quick brown fox' > "$root/plain"
	: > "$root/empty"
	printf 'abcde' > "$root/odd"		# 5 bytes, not a block multiple
	printf '%0999d' 7 > "$root/odd2"	# 999 bytes, spans two blocks
	ln -s plain "$root/symlink"
	ln "$root/plain" "$root/sub/hardlink"
	ln -s ../plain "$root/sub/parentlink"
	chmod 0751 "$root/plain"
}

cd "$work"

FIXTURE=$work/tarfixture
${CC:-cc} -std=c17 -O1 -Wall -Wextra -Werror -Wpedantic \
    -Wstrict-prototypes -Wold-style-definition -Wconversion -Wsign-conversion \
    -o "$FIXTURE" "$srcdir/tests/tarfixture.c" ||
    fail "host build of tarfixture.c"

extract_must_fail() {
	case_name=$1
	archive=$2
	root=$3
	if (cd "$root" && "$TAR" xf "$archive") >"$case_name.out" 2>&1; then
		fail "$case_name archive reported success"
	fi
}

list_must_fail() {
	case_name=$1
	archive=$2
	if "$TAR" tf "$archive" >"$case_name.out" 2>&1; then
		fail "$case_name archive listing reported success"
	fi
}

echo "tartest: extraction paths remain confined"
mkdir -p security/dotdot/root security/dotdot/outside
"$FIXTURE" dotdot > security/dotdot.tar
extract_must_fail dotdot "$work/security/dotdot.tar" \
    "$work/security/dotdot/root"
[ ! -e security/dotdot/escaped ] || fail "dotdot member escaped"

for scenario in dot empty-component control; do
	mkdir -p "security/$scenario/root"
	"$FIXTURE" "$scenario" > "security/$scenario.tar"
	extract_must_fail "$scenario" "$work/security/$scenario.tar" \
	    "$work/security/$scenario/root"
done

for scenario in symlink-control hardlink-control utf8-control \
    truncated-utf8-control; do
	"$FIXTURE" "$scenario" > "security/$scenario.tar"
	list_must_fail "$scenario" "$work/security/$scenario.tar"
done

mkdir -p security/absolute/root
"$FIXTURE" absolute "$work/security/absolute/escaped" \
    > security/absolute.tar
extract_must_fail absolute "$work/security/absolute.tar" \
    "$work/security/absolute/root"
[ ! -e security/absolute/escaped ] || fail "absolute member escaped"

mkdir -p security/parent/root security/parent/outside
printf 'sentinel\n' > security/parent/outside/payload
ln -s ../outside security/parent/root/link
mkdir -p security/parent-source/link
printf 'archive payload\n' > security/parent-source/link/payload
(cd security/parent-source && "$TAR" cf ../parent.tar link/payload)
extract_must_fail symlink-parent "$work/security/parent.tar" \
    "$work/security/parent/root"
grep -q '^sentinel$' security/parent/outside/payload ||
    fail "symlink parent changed its outside victim"

mkdir -p security/final/root security/final/outside
printf 'sentinel\n' > security/final/outside/victim
ln -s ../outside/victim security/final/root/victim
"$FIXTURE" final > security/final.tar
extract_must_fail final-symlink "$work/security/final.tar" \
    "$work/security/final/root"
grep -q '^sentinel$' security/final/outside/victim ||
    fail "final symlink changed its outside victim"

mkdir -p security/created/root/inside
"$FIXTURE" archive-symlink > security/created.tar
extract_must_fail archive-symlink "$work/security/created.tar" \
    "$work/security/created/root"
[ ! -e security/created/root/inside/from-archive ] ||
    fail "archive-created symlink was traversed by a later member"

mkdir -p security/symlink-mid-dotdot/root
"$FIXTURE" symlink-mid-dotdot > security/symlink-mid-dotdot.tar
extract_must_fail symlink-mid-dotdot \
    "$work/security/symlink-mid-dotdot.tar" \
    "$work/security/symlink-mid-dotdot/root"
[ ! -e security/symlink-mid-dotdot/root/link ] ||
    fail "symbolic link retained dot-dot after a named target component"

mkdir -p security/hardlink/root
printf 'sentinel\n' > security/hardlink/outside-existing
"$FIXTURE" hardlink > security/hardlink.tar
extract_must_fail hardlink-escape "$work/security/hardlink.tar" \
    "$work/security/hardlink/root"
[ ! -e security/hardlink/root/inside-link ] ||
    fail "unsafe hard-link target was created"

mkdir -p security/special/root
"$FIXTURE" special > security/special.tar
(cd security/special/root && "$TAR" xpf ../../special.tar)
[ ! -u security/special/root/setid ] &&
    [ ! -g security/special/root/setid ] ||
    fail "archive special mode bits survived extraction"

for scenario in unsupported directory-data bad-octal; do
	mkdir -p "security/$scenario/root"
	"$FIXTURE" "$scenario" > "security/$scenario.tar"
	extract_must_fail "$scenario" "$work/security/$scenario.tar" \
	    "$work/security/$scenario/root"
done

mkdir -p security/truncated/root
dd if=security/final.tar of=security/truncated.tar bs=1 count=600 2>/dev/null
extract_must_fail truncated "$work/security/truncated.tar" \
    "$work/security/truncated/root"

mkdir -p security/empty/root
: > security/empty.tar
extract_must_fail empty "$work/security/empty.tar" \
    "$work/security/empty/root"

mkdir -p security/existing/root
printf 'sentinel\n' > security/existing/root/victim
extract_must_fail existing "$work/security/final.tar" \
    "$work/security/existing/root"
grep -q '^sentinel$' security/existing/root/victim ||
    fail "existing regular output changed"

mkdir -p security/hard-source-symlink/root security/hard-source-symlink/outside
printf 'sentinel\n' > security/hard-source-symlink/outside/source
ln -s ../outside/source security/hard-source-symlink/root/source
"$FIXTURE" hardlink-source > security/hard-source-symlink.tar
extract_must_fail hard-source-symlink \
    "$work/security/hard-source-symlink.tar" \
    "$work/security/hard-source-symlink/root"
[ ! -e security/hard-source-symlink/root/inside-link ] ||
    fail "hard link followed a source symlink"

mkdir -p security/hard-source-existing/root
printf 'sentinel\n' > security/hard-source-existing/root/source
"$FIXTURE" hardlink-source > security/hard-source-existing.tar
extract_must_fail hard-source-existing \
    "$work/security/hard-source-existing.tar" \
    "$work/security/hard-source-existing/root"
[ ! -e security/hard-source-existing/root/inside-link ] ||
    fail "hard link admitted a pre-existing regular source"

mkdir -p security/hard-source-special/root
printf 'sentinel\n' > security/hard-source-special/root/source
chmod 6755 security/hard-source-special/root/source
"$FIXTURE" hardlink-source > security/hard-source-special.tar
extract_must_fail hard-source-special \
    "$work/security/hard-source-special.tar" \
    "$work/security/hard-source-special/root"
[ -u security/hard-source-special/root/source ] &&
    [ -g security/hard-source-special/root/source ] ||
    fail "hard-link rejection changed source special bits"

echo "tartest: streamed directories retain owner traversal"
mkdir -p restrictive-source/tree/sub restrictive-root
printf 'payload\n' > restrictive-source/tree/sub/file
(cd restrictive-source && "$TAR" cf ../restrictive.tar tree)
(umask 0777; cd restrictive-root && "$TAR" xf ../restrictive.tar)
chmod 0700 restrictive-root/tree
chmod 0700 restrictive-root/tree/sub
[ -f restrictive-root/tree/sub/file ] ||
    fail "restrictive umask blocked a streamed child"

echo "tartest: extracted directories finish with their sanitized modes"
mkdir -p mode-source/tree/sub mode-root
printf 'payload\n' > mode-source/tree/sub/file
chmod 0500 mode-source/tree mode-source/tree/sub
(cd mode-source && "$TAR" cf ../mode.tar tree)
(cd mode-root && "$TAR" xpf ../mode.tar)
[ "$(LC_ALL=C ls -ld mode-root/tree | cut -c1-10)" = "dr-x------" ] ||
    fail "-p did not restore the archived top-level directory mode"
[ "$(LC_ALL=C ls -ld mode-root/tree/sub | cut -c1-10)" = "dr-x------" ] ||
    fail "-p did not restore the archived nested directory mode"
chmod 0700 mode-source/tree mode-source/tree/sub
chmod 0700 mode-root/tree mode-root/tree/sub
mkdir -p zero-mode-root
"$FIXTURE" zero-directories > zero-mode.tar
(cd zero-mode-root && "$TAR" xpf ../zero-mode.tar)
[ "$(LC_ALL=C ls -ld zero-mode-root/tree | cut -c1-10)" = "d---------" ] ||
    fail "-p did not restore a mode-zero top-level directory"
chmod 0700 zero-mode-root/tree
[ "$(LC_ALL=C ls -ld zero-mode-root/tree/sub | cut -c1-10)" = "d---------" ] ||
    fail "-p did not restore a mode-zero nested directory"
chmod 0700 zero-mode-root/tree/sub
[ -f zero-mode-root/tree/sub/file ] ||
    fail "mode-zero directory finalization lost a streamed child"

echo "tartest: directory metadata survives noncontiguous members"
mkdir -p directory-revisit-root
"$FIXTURE" directory-revisit > directory-revisit.tar
(cd directory-revisit-root && "$TAR" xpf ../directory-revisit.tar)
[ "$(LC_ALL=C ls -ld directory-revisit-root/locked | cut -c1-10)" = \
    "d---------" ] || fail "revisited directory lost its final mode"
chmod 0700 directory-revisit-root/locked
grep -q '^payload$' directory-revisit-root/locked/file ||
    fail "revisited restrictive directory lost its later member"

echo "tartest: implicit parents finish with umask-derived modes"
mkdir -p implicit-parent-root
"$FIXTURE" implicit-parent > implicit-parent.tar
(umask 0777; cd implicit-parent-root && "$TAR" xf ../implicit-parent.tar)
[ "$(LC_ALL=C ls -ld implicit-parent-root/implicit | cut -c1-10)" = \
    "d---------" ] || fail "implicit top-level parent retained temporary mode"
chmod 0700 implicit-parent-root/implicit
[ "$(LC_ALL=C ls -ld implicit-parent-root/implicit/nested | cut -c1-10)" = \
    "d---------" ] || fail "implicit nested parent retained temporary mode"
chmod 0700 implicit-parent-root/implicit/nested
[ -f implicit-parent-root/implicit/nested/file ] ||
    fail "implicit-parent finalization lost the extracted member"

echo "tartest: repeated symbolic-link members retain the last target"
mkdir -p repeated-symlink-root
"$FIXTURE" repeated-symlink > repeated-symlink.tar
(cd repeated-symlink-root && "$TAR" xf ../repeated-symlink.tar)
[ "$(readlink repeated-symlink-root/link)" = "second" ] ||
    fail "repeated symbolic link did not retain its last target"

echo "tartest: repeated members may change type"
mkdir -p type-change-source type-change-root type-change-back-root \
    type-change-directory-root
printf 'regular first\n' > type-change-source/member
(cd type-change-source && "$TAR" cf ../type-change.tar member)
rm type-change-source/member
ln -s target type-change-source/member
(cd type-change-source && "$TAR" rf ../type-change.tar member)
(cd type-change-root && "$TAR" xf ../type-change.tar)
[ "$(readlink type-change-root/member)" = "target" ] ||
    fail "regular-to-symbolic-link replacement lost the last member"
(cd type-change-source && "$TAR" cf ../type-change-back.tar member)
rm type-change-source/member
printf 'regular last\n' > type-change-source/member
(cd type-change-source && "$TAR" rf ../type-change-back.tar member)
(cd type-change-back-root && "$TAR" xf ../type-change-back.tar)
grep -q '^regular last$' type-change-back-root/member ||
    fail "symbolic-link-to-regular replacement lost the last member"
rm type-change-source/member
printf 'regular before directory\n' > type-change-source/member
(cd type-change-source && "$TAR" cf ../type-change-directory.tar member)
rm type-change-source/member
mkdir type-change-source/member
printf 'directory child\n' > type-change-source/member/child
(cd type-change-source && "$TAR" rf ../type-change-directory.tar member)
(cd type-change-directory-root && "$TAR" xf ../type-change-directory.tar)
grep -q '^directory child$' type-change-directory-root/member/child ||
    fail "regular-to-directory replacement lost the last member"

echo "tartest: empty extracted directories may become regular files"
mkdir -p directory-change-source/member directory-change-root
(cd directory-change-source && "$TAR" cf ../directory-change.tar member)
rmdir directory-change-source/member
printf 'regular after directory\n' > directory-change-source/member
(cd directory-change-source && "$TAR" rf ../directory-change.tar member)
(cd directory-change-root && "$TAR" xf ../directory-change.tar)
grep -q '^regular after directory$' directory-change-root/member ||
    fail "directory-to-regular replacement lost the last member"
mkdir -p directory-change-existing/member
if (cd directory-change-existing && "$TAR" xf ../directory-change.tar) \
    >directory-change-existing.out 2>&1; then
    fail "directory replacement removed a pre-existing directory"
fi
[ -d directory-change-existing/member ] ||
    fail "directory replacement changed a pre-existing directory"
mkdir -p directory-change-nonempty-source/member directory-change-nonempty-root
printf 'retained child\n' > directory-change-nonempty-source/member/child
(cd directory-change-nonempty-source &&
    "$TAR" cf ../directory-change-nonempty.tar member)
(cd directory-change-source &&
    "$TAR" rf ../directory-change-nonempty.tar member)
if (cd directory-change-nonempty-root &&
    "$TAR" xf ../directory-change-nonempty.tar) \
    >directory-change-nonempty.out 2>&1; then
    fail "directory replacement removed a nonempty directory"
fi
grep -q '^retained child$' directory-change-nonempty-root/member/child ||
    fail "directory replacement lost a nonempty directory's child"

if [ "$(id -u)" -eq 0 ]; then
	echo "tartest: SKIP search-only fallbacks require a non-privileged UID"
else
	echo "tartest: write-and-search-only extraction root"
	mkdir -p search-only-root
	"$FIXTURE" implicit-parent > search-only.tar
	chmod 0300 search-only-root
	if ! (cd search-only-root && "$TAR" xf ../search-only.tar); then
		chmod 0700 search-only-root
		fail "write-and-search-only extraction root was rejected"
	fi
	chmod 0700 search-only-root
	grep -q '^payload$' search-only-root/implicit/nested/file ||
		fail "write-and-search-only extraction omitted its member"

	echo "tartest: write-and-search-only nested extraction directory"
	mkdir -p search-only-nested-source/locked search-only-nested-root/locked
	printf 'nested payload\n' > search-only-nested-source/locked/file
	(cd search-only-nested-source &&
		"$TAR" cf ../search-only-nested.tar locked/file)
	chmod 0333 search-only-nested-root/locked
	if ! (cd search-only-nested-root &&
		"$TAR" xf ../search-only-nested.tar); then
		chmod 0700 search-only-nested-root/locked
		fail "write-and-search-only nested directory was rejected"
	fi
	chmod 0700 search-only-nested-root/locked
	grep -q '^nested payload$' search-only-nested-root/locked/file ||
		fail "write-and-search-only nested directory omitted its member"

	echo "tartest: explicit write-and-search-only directory member"
	mkdir -p search-only-explicit-source/locked \
		search-only-explicit-root/locked
	printf 'explicit payload\n' > search-only-explicit-source/locked/file
	(cd search-only-explicit-source &&
		"$TAR" cf ../search-only-explicit.tar locked)
	chmod 0333 search-only-explicit-root/locked
	if ! (cd search-only-explicit-root &&
		"$TAR" xf ../search-only-explicit.tar); then
		chmod 0700 search-only-explicit-root/locked
		fail "explicit search-only directory member was rejected"
	fi
	chmod 0700 search-only-explicit-root/locked
	grep -q '^explicit payload$' search-only-explicit-root/locked/file ||
		fail "explicit search-only directory omitted its member"

	echo "tartest: search-only archive-creation root"
	mkdir -p create-search-only-root
	printf 'known input\n' > create-search-only-root/payload
	chmod 0100 create-search-only-root
	if ! (cd create-search-only-root &&
		"$TAR" cf ../create-search-only.tar payload); then
		chmod 0700 create-search-only-root
		fail "search-only archive-creation root was rejected"
	fi
	chmod 0700 create-search-only-root
	[ "$("$TAR" tf create-search-only.tar)" = "payload" ] ||
		fail "search-only archive creation omitted the named input"

	echo "tartest: search-only creation restores its root after -h traversal"
	mkdir -p create-search-follow-root create-search-follow-target
	printf 'followed input\n' > create-search-follow-target/input
	printf 'root input\n' > create-search-follow-root/payload
	ln -s ../create-search-follow-target create-search-follow-root/link
	chmod 0100 create-search-follow-root
	if ! (cd create-search-follow-root &&
		"$TAR" chf ../create-search-follow.tar link/input payload); then
		chmod 0700 create-search-follow-root
		fail "search-only followed traversal lost the creation root"
	fi
	chmod 0700 create-search-follow-root
	"$TAR" tf create-search-follow.tar > create-search-follow.list
	grep -q '^link/input$' create-search-follow.list ||
		fail "search-only followed traversal omitted its input"
	grep -q '^payload$' create-search-follow.list ||
		fail "search-only followed traversal omitted the later root input"
fi

echo "tartest: archive creation verifies source paths"
mkdir -p create-source/root create-source/outside
printf 'outside\n' > create-source/outside/payload
ln -s ../outside create-source/root/link
if (cd create-source/root &&
    "$TAR" cf "$work/create-symlink.tar" link/payload) >create.out 2>&1; then
	fail "archive creation followed a symlinked parent"
fi
(cd create-source/root &&
    "$TAR" chf "$work/create-follow.tar" link/payload) ||
    fail "archive creation -h did not follow a symlinked parent"
"$TAR" tf create-follow.tar | grep -q '^link/payload$' ||
    fail "archive creation -h omitted the followed input"
mkdir -p create-trailing/tree
printf 'trailing slash\n' > create-trailing/tree/payload
(cd create-trailing && "$TAR" cf ../create-trailing.tar tree/) ||
    fail "archive creation rejected a trailing slash"
"$TAR" tf create-trailing.tar | grep -q '^tree/payload$' ||
    fail "trailing-slash input omitted its child"
mkdir -p create-dot/source/tree create-dot/target
printf 'dot operand\n' > create-dot/source/tree/payload
(cd create-dot/source && "$TAR" cf - .) |
    (cd create-dot/target && "$TAR" xpf -) ||
    fail "the documented current-directory copy invocation failed"
diff -r create-dot/source create-dot/target ||
    fail "the current-directory copy invocation changed the tree"
mkdir -p create-self
printf 'bounded\n' > create-self/payload
(cd create-self && "$TAR" cf archive.tar .) ||
    fail "current-directory creation with an in-tree archive failed"
"$TAR" tf create-self/archive.tar > create-self.list
grep -q '^payload$' create-self.list ||
    fail "current-directory creation omitted its payload"
if grep -q '^archive.tar$' create-self.list; then
	fail "current-directory creation archived its own growing output"
fi
mkdir -p create-links
ln -s /etc/passwd create-links/absolute
ln -s ../outside create-links/escape
printf 'safe source\n' > create-links/safe
if (cd create-links &&
    "$TAR" cf "$work/create-absolute-link.tar" absolute safe) \
    >create-absolute-link.out 2>&1; then
	fail "archive creation accepted an absolute symbolic-link target"
fi
[ "$("$TAR" tf create-absolute-link.tar)" = "safe" ] ||
	fail "unsafe symbolic link prevented a complete recoverable archive"
if (cd create-links &&
    "$TAR" cf "$work/create-escape-link.tar" escape) \
    >create-escape-link.out 2>&1; then
	fail "archive creation accepted an escaping symbolic-link target"
fi
printf 'safe operand\n' > create-safe
if "$TAR" cf create-absolute.tar "$work/create-source/outside/payload" \
    create-safe \
    >create-absolute.out 2>&1; then
	fail "archive creation accepted an absolute input path"
fi
[ "$("$TAR" tf create-absolute.tar)" = "create-safe" ] ||
	fail "unsafe input path prevented a complete recoverable archive"
control_name=$(printf 'line\nbreak')
mkdir -p create-control
printf 'unsafe source\n' > "create-control/$control_name"
printf 'safe source\n' > create-control/safe
if (cd create-control &&
    "$TAR" cf "$work/create-control.tar" "$control_name" safe) \
    >create-control.out 2>&1; then
	fail "archive creation accepted a control-bearing input path"
fi
printf 'tar: unsafe source omitted\n' > create-control.expected
cmp create-control.expected create-control.out ||
	fail "unsafe source diagnostic exposed the rejected pathname"
[ "$("$TAR" tf create-control.tar)" = "safe" ] ||
	fail "control-bearing input prevented a complete recoverable archive"
if "$TAR" cf create-missing.tar missing-input >create-missing.out 2>&1; then
	fail "archive creation reported success for a missing input"
fi

echo "tartest: valid UTF-8 path bytes survive create, list and extract"
utf8_name=$(printf '\342\202\254')
mkdir -p utf8-source utf8-root
printf 'UTF-8 payload\n' > "utf8-source/$utf8_name"
(cd utf8-source && "$TAR" cf ../utf8.tar "$utf8_name")
[ "$("$TAR" tf utf8.tar)" = "$utf8_name" ] ||
	fail "valid UTF-8 filename changed during listing"
(cd utf8-root && "$TAR" xf ../utf8.tar)
cmp "utf8-source/$utf8_name" "utf8-root/$utf8_name" ||
    fail "valid UTF-8 filename failed to round trip"
: "${PYTHON:?set PYTHON to the intended interpreter}"
"$PYTHON" - "$TAR" <<'PY'
import os
import subprocess
import sys

tar = os.fsencode(sys.argv[1])
name = b"\xe9"
source_path = b"utf8-source/" + name
root_path = b"utf8-root/" + name
with open(source_path, "wb") as source_file:
    source_file.write(b"opaque payload\n")
subprocess.run([tar, b"cf", b"../opaque.tar", name], cwd=b"utf8-source", check=True)
listing = subprocess.run(
    [tar, b"tf", b"opaque.tar"], check=True, stdout=subprocess.PIPE
).stdout
if listing != name + b"\n":
    raise SystemExit("opaque non-control filename changed during listing")
subprocess.run([tar, b"xf", b"../opaque.tar"], cwd=b"utf8-root", check=True)
with open(source_path, "rb") as source_file, open(root_path, "rb") as extracted_file:
    if source_file.read() != extracted_file.read():
        raise SystemExit("opaque non-control filename failed to round trip")
print("tartest: opaque non-control filename survives create, list and extract")
PY
if "$TAR" cf create-chdir.tar -C missing-directory payload \
    >create-chdir.out 2>&1; then
	fail "archive creation reported success after a failed -C"
fi

echo "tartest: appended and updated members restore the last entry"
mkdir -p padded-time-source
printf 'older payload\n' > 'padded-time-source/padded member'
touch -t 200001010000 'padded-time-source/padded member'
"$FIXTURE" padded-time > padded-time.tar
(cd padded-time-source && "$TAR" uf ../padded-time.tar 'padded member')
[ "$("$TAR" tf padded-time.tar | grep -c '^padded member$')" -eq 1 ] ||
    fail "update appended an older member with space-padded archive time"
printf 'prefix payload\n' > 'padded-time-source/member 17777777777'
printf 'distinct payload\n' > padded-time-source/member
(cd padded-time-source &&
    "$TAR" cf ../update-prefix.tar 'member 17777777777')
(cd padded-time-source && "$TAR" uf ../update-prefix.tar member)
"$TAR" tf update-prefix.tar > update-prefix.list
grep -q '^member$' update-prefix.list ||
    fail "update mistook an octal filename suffix for a timestamp"
echo "tartest: native host update lookup bounds long recursive names"
HOST_WIDTH_TAR=$work/tar-host-width
${CC:-cc} -D_DEFAULT_SOURCE -std=c17 -O1 \
    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
    -Wold-style-definition -Wconversion -Wsign-conversion \
    -fno-omit-frame-pointer -fsanitize=address,undefined \
    -o "$HOST_WIDTH_TAR" "$srcdir/tar.c" ||
    fail "native path-width host build of tar.c"
mkdir -p "native-update-source/$deep"
long_leaf=abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqr
printf 'long payload\n' > "native-update-source/$deep/$long_leaf"
printf 'index payload\n' > native-update-source/index
(cd native-update-source && "$TAR" cf ../native-update.tar index)
if (cd native-update-source && "$HOST_WIDTH_TAR" uf ../native-update.tar .) \
    >native-update.out 2>&1; then
    fail "native host update accepted a path beyond the archive format"
fi
grep -q 'file name too long' native-update.out ||
    fail "native host update failed before format-length rejection"
mkdir -p repeated append-root update-root
printf 'first\n' > repeated/member
(cd repeated && "$TAR" cf ../append.tar member)
printf 'appended\n' > repeated/member
(cd repeated && "$TAR" rf ../append.tar member)
(cd append-root && "$TAR" xf ../append.tar)
grep -q '^appended$' append-root/member ||
    fail "append extraction did not retain the last member"
(cd repeated && "$TAR" cf ../update.tar member)
printf 'updated\n' > repeated/member
touch -t 203001010000 repeated/member
(cd repeated && "$TAR" uf ../update.tar member)
(cd update-root && "$TAR" xf ../update.tar)
grep -q '^updated$' update-root/member ||
    fail "update extraction did not retain the last member"
mkdir -p repeated-hardlink-root
"$FIXTURE" repeated-hardlink > repeated-hardlink.tar
(cd repeated-hardlink-root && "$TAR" xf ../repeated-hardlink.tar)
grep -q '^new payload$' repeated-hardlink-root/source ||
    fail "repeated regular member did not replace its earlier inode"
grep -q '^old payload$' repeated-hardlink-root/alias ||
    fail "regular replacement changed an earlier hard-link alias"
ls -li repeated-hardlink-root/alias repeated-hardlink-root/alias2 \
    > repeated-hardlink.stat
[ "$(awk '{print $1}' repeated-hardlink.stat | sort -u | wc -l)" -eq 1 ] ||
    fail "replacement forgot a still-live extracted hard-link identity"

echo "tartest: append retains a host archive's one-record block factor"
mkdir -p block-factor
printf 'first\n' > block-factor/first
printf 'second\n' > block-factor/second
(cd block-factor &&
    "$HOSTTAR" ${HOSTFMT:+"$HOSTFMT"} -cf ../block-factor.tar -b 1 first)
(cd block-factor && "$TAR" rf ../block-factor.tar second)
"$TAR" tf block-factor.tar > block-factor.self-list
"$HOSTTAR" tf block-factor.tar > block-factor.host-list
for listing in block-factor.self-list block-factor.host-list; do
	grep -q '^first$' "$listing" || fail "$listing omitted the first member"
	grep -q '^second$' "$listing" || fail "$listing omitted the appended member"
done
mkdir -p block-factor-refill
for name in first second third fourth; do
	printf '%s\n' "$name" > "block-factor-refill/$name"
done
(cd block-factor-refill &&
    "$HOSTTAR" ${HOSTFMT:+"$HOSTFMT"} -cf ../block-factor-refill.tar \
    -b 1 first second third fourth)
printf 'appended\n' > block-factor-refill/appended
(cd block-factor-refill && "$TAR" rf ../block-factor-refill.tar appended)
"$TAR" tf block-factor-refill.tar > block-factor-refill.self-list
"$HOSTTAR" tf block-factor-refill.tar > block-factor-refill.host-list
for listing in block-factor-refill.self-list block-factor-refill.host-list; do
	for name in first second third fourth appended; do
		grep -q "^$name$" "$listing" || fail "$listing omitted $name"
	done
done

echo "tartest: fatal compressed-input errors stop the filter promptly"
FILTER_TAR=$work/tar-filter
${CC:-cc} -D_DEFAULT_SOURCE -DTAR_PATH_LIMIT=256 -std=c17 -O1 \
    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
    -Wold-style-definition -Wconversion -Wsign-conversion \
    -fno-omit-frame-pointer -fsanitize=address,undefined \
    -DCOMPRESS=\"$FIXTURE\" -o "$FILTER_TAR" "$srcdir/tar.c" ||
    fail "host build of tar.c with the nonterminating filter fixture"
: > filter-input
mkdir filter-root
(cd filter-root && "$FILTER_TAR" xZf ../filter-input) \
    >filter.out 2>&1 &
filter_pid=$!
(sleep 5; : > filter-timeout; kill "$filter_pid" 2>/dev/null || :) &
watchdog_pid=$!
if wait "$filter_pid"; then
	kill "$watchdog_pid" 2>/dev/null || :
	wait "$watchdog_pid" 2>/dev/null || :
	fail "malformed compressed archive reported success"
fi
kill "$watchdog_pid" 2>/dev/null || :
wait "$watchdog_pid" 2>/dev/null || :
[ ! -e filter-timeout ] || fail "fatal parse error drained the filter tail"
grep -q 'malformed octal field' filter.out ||
    fail "compressed-input fixture did not reach the fatal parser path"

#
# ustar: the full tree, long path included.
#
#
# A link target of exactly 100 bytes fills the linkname field, which then
# carries no terminator: read in place it runs on into the ustar magic and
# the link comes back with "ustar" appended. The target has to exist, since
# diff -r follows the link. The v7 tree leaves this out: the path is 105
# bytes, which that header cannot hold at all.
#
full=L
while [ ${#full} -lt 91 ]; do
	full=$full/aaaaaaaaa
done
full=$full/ffffffff
[ ${#full} -eq 100 ] || fail "the link target is ${#full} bytes, not 100"

mkdir -p ustar
(cd ustar && mkshort tree && mkdir -p "tree/$deep" &&
    echo 'buried' > "tree/$deep/leaf.txt" &&
    mkdir -p "tree/$(dirname "$full")" && echo 'linked-to' > "tree/$full" &&
    ln -s "$full" tree/fulltarget)
(cd ustar && "$TAR" cf ../u.tar tree)

#
# Two runs over an unchanged tree must produce the same bytes. The block a
# short file's data ends in is written whole, so anything left unset in it
# is heap contents, which differ run to run and leak into the archive.
#
echo "tartest: archive bytes are reproducible"
(cd ustar && "$TAR" cf ../u2.tar tree)
cmp -s u.tar u2.tar || fail "two runs over one tree wrote different bytes"
rm -f u2.tar

echo "tartest: ustar magic check"
dd if=u.tar bs=1 skip=257 count=5 2>/dev/null | grep -q '^ustar$' ||
    fail "no ustar magic at offset 257"

echo "tartest: ustar list"
"$TAR" tf u.tar > u.list
grep -q "leaf.txt" u.list || fail "long path missing from the listing"

echo "tartest: ustar extract by the tool under test"
mkdir -p x-self
(cd x-self && "$TAR" xf ../u.tar)
diff -r ustar/tree x-self/tree || fail "ustar self round trip differs"

echo "tartest: ustar listed by the host tar"
"$HOSTTAR" tf u.tar > u.hostlist 2>u.hosterr ||
    fail "host tar rejected the ustar archive: $(cat u.hosterr)"
[ ! -s u.hosterr ] || fail "host tar warned on the archive: $(cat u.hosterr)"
grep -q "leaf.txt" u.hostlist ||
    fail "host tar did not see the prefix-split path"
[ "$(readlink x-self/tree/fulltarget | wc -c)" -eq 101 ] ||
    fail "the 100-byte link target did not survive: \
$(readlink x-self/tree/fulltarget)"

echo "tartest: ustar extracted by the host tar"
mkdir -p x-host
(cd x-host && "$HOSTTAR" xf ../u.tar)
diff -r ustar/tree x-host/tree || fail "host tar extract differs"

echo "tartest: archive written by the host tar read by the tool under test"
(cd ustar && "$HOSTTAR" ${HOSTFMT:+"$HOSTFMT"} -cf ../h.tar tree)
mkdir -p x-back
(cd x-back && "$TAR" xf ../h.tar)
diff -r ustar/tree x-back/tree || fail "reading the host tar archive differs"

#
# v7: the short tree only. The v7 header has no prefix field, so a path
# over 99 bytes has no representation at all and the tool refuses it; that
# refusal is asserted rather than archived.
#
mkdir -p v7
(cd v7 && mkshort tree)
(cd v7 && "$TAR" cOf ../v.tar tree)

echo "tartest: v7 header carries no magic"
dd if=v.tar bs=1 skip=257 count=5 2>/dev/null | grep -q '^ustar$' &&
    fail "v7 archive carries the ustar magic"

echo "tartest: v7 extract by the tool under test"
mkdir -p x-v7
(cd x-v7 && "$TAR" xf ../v.tar)
diff -r v7/tree x-v7/tree || fail "v7 round trip differs"

echo "tartest: v7 archive listed by the host tar"
"$HOSTTAR" tf v.tar > v.hostlist || fail "host tar rejected the v7 archive"
grep -q "tree/plain" v.hostlist || fail "host tar did not see tree/plain"

echo "tartest: v7 refuses a path the header cannot hold"
(cd ustar && "$TAR" cOf ../vlong.tar tree) > vlong.out 2>&1 || true
grep -q "file name too long" vlong.out ||
    fail "v7 accepted a path over 99 bytes"

#
# ustar splits a path at a slash, so a single component over NAMSIZ-1 bytes
# has no representation whatever the prefix field holds. Refusing it is the
# answer; writing an entry with an empty name field is not, because
# endtape() reads an empty name as the end of the archive and every entry
# after it is lost. The file after the refused one proves which happened.
#
echo "tartest: a component over the name field is refused, not truncated"
long=llllllllllmmmmmmmmmmnnnnnnnnnnoooooooooopppppppppp
long=$long$long$long
mkdir -p toolong/tree/"$long"
echo 'buried' > toolong/tree/"$long"/leaf
echo 'after' > toolong/tree/zzz-after
(cd toolong && "$TAR" cf ../t.tar tree) > t.out 2>&1 || true
grep -q "file name too long" t.out ||
    fail "a 150-byte path component was not refused"
"$TAR" tf t.tar > t.list
grep -q "tree/zzz-after" t.list ||
    fail "entries after the refused name were lost"
"$HOSTTAR" tf t.tar > /dev/null 2>&1 ||
    fail "host tar rejected the archive around the refused name"

echo "tartest: both formats read without a format flag"
"$TAR" tf u.tar > /dev/null || fail "reading ustar needs a flag"
"$TAR" tf v.tar > /dev/null || fail "reading v7 needs a flag"

#
# -z and -Z pipe the archive through the LZW filter as a separate process
# rather than linking the codec in, so a truncated result means tar exited
# before the child drained; the diff is what catches that.
#
if [ -n "$ZPROG" ] && [ -z "${1:-}" ]; then
	echo "tartest: LZW filter through $ZPROG"
	(cd ustar && "$TAR" cZf ../z.tar tree)
	#
	# The decompressed stream is a plain archive the host tar reads.
	# It is not byte-identical to u.tar: getbuf() takes the blocking
	# factor from the archive fd's st_blksize, which differs between a
	# pipe and a regular file, so the trailing pad differs in length.
	#
	"$ZPROG" -d < z.tar > z.plain
	mkdir -p x-zplain
	(cd x-zplain && "$HOSTTAR" xf ../z.plain)
	diff -r ustar/tree x-zplain/tree ||
	    fail "the decompressed -Z archive differs"
	[ "$(wc -c < z.tar)" -lt "$(wc -c < u.tar)" ] ||
	    fail "-Z did not compress the archive"
	mkdir -p x-z
	(cd x-z && "$TAR" xZf ../z.tar)
	diff -r ustar/tree x-z/tree || fail "-Z round trip differs"

	cp u.tar z-tail.plain
	dd if=/dev/zero bs=1024 count=256 >> z-tail.plain 2>/dev/null
	"$ZPROG" < z-tail.plain > z-tail.tar
	mkdir -p x-z-tail
	(cd x-z-tail && "$TAR" xZf ../z-tail.tar)
	diff -r ustar/tree x-z-tail/tree ||
	    fail "-Z archive with trailing output differs"

	(cd ustar && "$TAR" czf ../z2.tar tree)
	cmp -s z2.tar z.tar || fail "-z and -Z wrote different archives"
	rm -f z2.tar
	echo "tartest: -Z archive is $(wc -c < z.tar) bytes against \
$(wc -c < u.tar) uncompressed"
else
	echo "tartest: SKIP the LZW filter: no compress on this host"
fi

#
# Hard links. A create run keeps one identity per multi-linked inode for the
# whole of the run, because a second link to an inode is written as an
# LNKTYPE entry naming the first, and the identities accumulate for as long
# as tar writes. The tree here is many distinct inodes rather than many links
# to one, which is what the list length follows, and the base names differ in
# length so the path allocation is exercised at more than one size.
#
# NIDENT bounds the run at a few seconds' work. The footprint the list must
# not have -- one that follows the widest representable path rather than the
# paths present -- is pinned at build time by the assertion on the node width
# in tar.c, since the window a create run exhausts is the board's, not this
# host's.
#
NIDENT=64
LINKBYTES=2048			# four records, so a second copy is visible
NBLOCK=20			# tar.c's NBLOCK, the records an archive pads to
pad=abcdefghijklmnopqrstuvwxyz

mkdir -p links
i=1
while [ "$i" -le "$NIDENT" ]; do
	name=f$i
	j=0
	while [ "$j" -lt $(( i % 3 )) ]; do
		name=$name$pad
		j=$(( j + 1 ))
	done
	mkdir -p "links/tree/d$i"
	printf "%0${LINKBYTES}d" "$i" > "links/tree/d$i/$name"
	ln "links/tree/d$i/$name" "links/tree/d$i/$name.l1"
	ln "links/tree/d$i/$name" "links/tree/d$i/$name.l2"
	echo "tree/d$i/$name" >> links/names
	i=$(( i + 1 ))
done

echo "tartest: $NIDENT hard-link identities held through one create run"
(cd links && "$TAR" cvf ../l.tar tree) > l.out 2> l.err
grep -q 'out of memory' l.err &&
    fail "the link list ran out of memory at $NIDENT identities"
[ "$(grep -c ' link to ' l.out)" -eq $(( NIDENT * 2 )) ] ||
    fail "$(grep -c ' link to ' l.out) of $(( NIDENT * 2 )) further links \
were written as link entries"

#
# An identity the list dropped turns its further links into full copies of
# the file, so the archive's size is what says whether the list held. Every
# entry costs one record of header and only the first link to an inode
# carries data, so the bound is one copy of the data, a record for each of
# the four entries an identity contributes, the two-record trailer and the
# blocking pad. Three copies of the data is what a dropped identity costs.
#
echo "tartest: the archive carries one copy of each multi-linked file"
lbound=$(( NIDENT * LINKBYTES + NIDENT * 4 * 512 + 2 * NBLOCK * 512 ))
[ "$(wc -c < l.tar)" -lt "$lbound" ] ||
    fail "the archive is $(wc -c < l.tar) bytes against the $lbound one \
copy of the file data allows"

echo "tartest: the extracted links are one inode each"
mkdir -p x-links
(cd x-links && "$TAR" xf ../l.tar)
diff -r links/tree x-links/tree || fail "the hard-link tree round trip differs"
while read -r p; do
	set -- "x-links/$p" "x-links/$p.l1" "x-links/$p.l2"
	ls -li "$@" > l.stat
	[ "$(awk '{print $1}' l.stat | sort -u | wc -l)" -eq 1 ] ||
	    fail "$p came back as separate inodes"
	[ "$(awk 'NR == 1 {print $3}' l.stat)" -eq 3 ] ||
	    fail "$p came back with $(awk 'NR == 1 {print $3}' l.stat) links"
done < links/names

echo "tartest: the host tar restores the same link sets"
mkdir -p xh-links
(cd xh-links && "$HOSTTAR" xf ../l.tar)
while read -r p; do
	[ "$(ls -l "xh-links/$p" | awk '{print $2}')" -eq 3 ] ||
	    fail "the host tar gave $p $(ls -l "xh-links/$p" | \
awk '{print $2}') links"
done < links/names

#
# -l reports the links an archive leaves out, reading each identity's stored
# path after the run rather than during it. The name is long enough that a
# path stored short of its length reads as a different path here.
#
echo "tartest: -l names the full path of a link left out of the archive"
lone=aaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeeeffffffffff
mkdir -p lone/tree/sub
echo 'two links' > "lone/tree/$lone"
ln "lone/tree/$lone" lone/tree/sub/other
(cd lone && "$TAR" clf ../lone.tar "tree/$lone") 2> lone.err ||
    fail "the -l create run failed: $(cat lone.err)"
grep -q "^tar: missing links to tree/$lone\$" lone.err ||
    fail "-l did not name the missing link's path: $(cat lone.err)"

echo "tartest: PASS"
