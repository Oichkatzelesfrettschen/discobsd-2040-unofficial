#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"

source_directory=$(cd "$(dirname "$0")/.." && pwd)
work_directory=$(mktemp -d "${TMPDIR:-/tmp}/tar-timestamp-identity.XXXXXX")
trap 'rm -rf "$work_directory"' EXIT INT HUP TERM

fail() {
    echo "tar timestamp identity: FAIL: $*" >&2
    exit 1
}

compiler=${CC:-cc}
common_flags="-D_DEFAULT_SOURCE -DTAR_PATH_LIMIT=256 -std=c17 -O1 \
    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
    -Wold-style-definition -Wconversion -Wsign-conversion \
    -include $source_directory/tests/rooted_link_host.h"

$compiler $common_flags -Dfutimes=tar_test_futimes \
    -Dutimes=tar_test_utimes -c -o "$work_directory/tar.o" \
    "$source_directory/tar.c" || fail "compile tar source"
$compiler $common_flags -c -o "$work_directory/timestamp_identity_shim.o" \
    "$source_directory/tests/timestamp_identity_shim.c" ||
    fail "compile timestamp identity shim"
$compiler $common_flags -c -o "$work_directory/rooted_link_host.o" \
    "$source_directory/tests/rooted_link_host.c" ||
    fail "compile rooted link host shim"
$compiler -o "$work_directory/tar" "$work_directory/tar.o" \
    "$work_directory/timestamp_identity_shim.o" \
    "$work_directory/rooted_link_host.o" ||
    fail "link timestamp identity fixture"

run_case() {
    binary=$1
    kind=$2
    label=$3
    case_directory=$work_directory/$label-$kind
    source_root=$case_directory/source
    extraction_root=$case_directory/extract
    archive=$case_directory/archive.tar

    mkdir -p "$source_root" "$extraction_root"
    if [ "$kind" = file ]; then
        printf 'archive payload\n' > "$source_root/member"
    else
        mkdir "$source_root/member"
    fi
    "$PYTHON" - "$source_root/member" <<'PY'
import os
import sys

os.utime(sys.argv[1], (946684800, 946684800))
PY
    tar --format=ustar -cf "$archive" -C "$source_root" member
    (
        cd "$extraction_root"
        TAR_TIMESTAMP_ORIGINAL=member \
        TAR_TIMESTAMP_MOVED=moved-member \
        TAR_TIMESTAMP_KIND=$kind \
            "$binary" xf "$archive"
    ) || {
        echo "$label $kind extraction failed" >&2
        return 1
    }
    "$PYTHON" - "$kind" "$extraction_root/member" \
        "$extraction_root/moved-member" <<'PY' || return 1
import pathlib
import sys

kind = sys.argv[1]
replacement = pathlib.Path(sys.argv[2])
moved = pathlib.Path(sys.argv[3])
expected_time = 946684800
if int(moved.stat().st_mtime) != expected_time:
    raise SystemExit("moved object did not receive the archive timestamp")
if int(replacement.stat().st_mtime) == expected_time:
    raise SystemExit("replacement object received the archive timestamp")
if kind == "file":
    if moved.read_text(encoding="utf-8") != "archive payload\n":
        raise SystemExit("moved file lost the archive payload")
    if replacement.read_text(encoding="utf-8") != "replacement\n":
        raise SystemExit("replacement file contents changed")
PY
}

run_case "$work_directory/tar" file descriptor || fail "file identity"
run_case "$work_directory/tar" directory descriptor || fail "directory identity"

"$PYTHON" - "$source_directory/tar.c" \
    "$work_directory/tar_pathname_mutant.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
needle = "if (futimes(descriptor, tv) < 0) {"
if source.count(needle) != 1:
    raise SystemExit("timestamp helper shape changed")
Path(sys.argv[2]).write_text(
    source.replace(
        needle,
        "(void)descriptor;\n    if (utimes(display_path, tv) < 0) {",
    ),
    encoding="utf-8",
)
PY
$compiler $common_flags -Dfutimes=tar_test_futimes \
    -Dutimes=tar_test_utimes -c -o "$work_directory/tar_pathname_mutant.o" \
    "$work_directory/tar_pathname_mutant.c" || fail "compile pathname mutant"
$compiler -o "$work_directory/tar_pathname_mutant" \
    "$work_directory/tar_pathname_mutant.o" \
    "$work_directory/timestamp_identity_shim.o" \
    "$work_directory/rooted_link_host.o" || fail "link pathname mutant"

mutant_output=$work_directory/pathname-mutant.out
if run_case "$work_directory/tar_pathname_mutant" file pathname-mutant \
    >"$mutant_output" 2>&1; then
    fail "pathname mutation preserved descriptor identity"
else
    mutant_status=$?
fi
if [ "$mutant_status" -ne 1 ] ||
    ! grep -F "moved object did not receive the archive timestamp" \
        "$mutant_output" >/dev/null; then
    cat "$mutant_output" >&2
    fail "pathname mutation missed its deciding assertion"
fi
echo "tar timestamp identity: pathname mutation rejected"
echo "tar timestamp identity: PASS"
