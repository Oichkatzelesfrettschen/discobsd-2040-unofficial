#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1
export ASAN_OPTIONS UBSAN_OPTIONS

source_directory=$(cd "$(dirname "$0")/.." && pwd)
work_directory=$(mktemp -d "${TMPDIR:-/tmp}/tar-hardlink-identity.XXXXXX")
trap 'rm -rf "$work_directory"' EXIT INT HUP TERM

fail() {
    echo "tar hard-link identity: FAIL: $*" >&2
    exit 1
}

compiler=${CC:-cc}
common_flags="-D_DEFAULT_SOURCE -DTAR_PATH_LIMIT=256 -std=c17 -O1 \
    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
    -Wold-style-definition -Wconversion -Wsign-conversion \
    -fno-omit-frame-pointer -fsanitize=address,undefined \
    -include $source_directory/tests/rooted_link_host.h"

$compiler $common_flags -o "$work_directory/tar" \
    "$source_directory/tar.c" \
    "$source_directory/tests/rooted_link_host.c" ||
    fail "compile descriptor-conditioned tar"
$compiler -std=c17 -O1 -Wall -Wextra -Werror -Wpedantic \
    -Wstrict-prototypes -Wold-style-definition -Wconversion \
    -Wsign-conversion -o "$work_directory/tarfixture" \
    "$source_directory/tests/tarfixture.c" ||
    fail "compile tar fixture"
"$work_directory/tarfixture" hardlink-race > "$work_directory/archive.tar"

run_rejection_case() {
    binary=$1
    replacement_kind=$2
    label=$3
    abort_after_substitution=${4:-no}
    extraction_root=$work_directory/$label
    output=$work_directory/$label.out

    mkdir "$extraction_root"
    set +e
    (
        cd "$extraction_root"
        if [ "$abort_after_substitution" = yes ]; then
            TAR_LINK_ABORT_AFTER_SUBSTITUTION=1
            export TAR_LINK_ABORT_AFTER_SUBSTITUTION
        else
            unset TAR_LINK_ABORT_AFTER_SUBSTITUTION
        fi
        TAR_LINK_TRACE=1 \
        TAR_LINK_MOVED_SOURCE=moved-source \
        TAR_LINK_REPLACEMENT_KIND=$replacement_kind \
            "$binary" xf "$work_directory/archive.tar"
    ) >"$output" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 2 ]; then
        return 1
    fi
    grep -F "tar hard-link identity: rooted call" "$output" >/dev/null ||
        return 1
    grep -F "tar: create hard link alias:" "$output" >/dev/null ||
        return 1
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:' \
        "$output" >/dev/null; then
        return 1
    fi
    grep -q '^archive source$' "$extraction_root/moved-source" ||
        return 1
    if [ -e "$extraction_root/alias" ] || [ -L "$extraction_root/alias" ]; then
        return 1
    fi
    if [ "$replacement_kind" = regular ]; then
        grep -q '^foreign replacement$' "$extraction_root/source" ||
            return 1
    else
        [ "$(readlink "$extraction_root/source")" = foreign-target ] ||
            return 1
    fi
}

run_rejection_case "$work_directory/tar" regular regular-source ||
    fail "regular source substitution reported success"
run_rejection_case "$work_directory/tar" symlink symlink-source ||
    fail "symbolic-link source substitution reported success"
if run_rejection_case "$work_directory/tar" regular abort-control yes; then
    fail "abort after source substitution satisfied the rejection contract"
fi
echo "tar hard-link identity: abort control rejected"

"$PYTHON" - "$source_directory/tar.c" "$work_directory/tar_pathname_mutant.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = """            link_result = linkat_beneath(extraction_root_descriptor,
                curlink, source_descriptor, curname);"""
new = """            link_result = tar_test_path_link(curlink, curname);"""
if source.count(old) != 1:
    raise SystemExit("hard-link pathname mutation anchor count differs")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
$compiler $common_flags -o "$work_directory/tar_pathname_mutant" \
    "$work_directory/tar_pathname_mutant.c" \
    "$source_directory/tests/rooted_link_host.c" ||
    fail "compile pathname hard-link mutation"

mutant_root=$work_directory/pathname-mutant
mutant_output=$work_directory/pathname-mutant.out
mkdir "$mutant_root"
if ! (
    cd "$mutant_root"
    TAR_LINK_TRACE=1 \
    TAR_LINK_MOVED_SOURCE=moved-source \
    TAR_LINK_REPLACEMENT_KIND=regular \
        "$work_directory/tar_pathname_mutant" xf "$work_directory/archive.tar"
) >"$mutant_output" 2>&1; then
    cat "$mutant_output" >&2
    fail "pathname hard-link mutation did not reproduce publication"
fi
grep -F "tar hard-link identity: pathname call" "$mutant_output" >/dev/null ||
    fail "pathname mutation did not reach its scheduled hook"
grep -q '^archive source$' "$mutant_root/moved-source" ||
    fail "pathname mutation lost the moved archive source"
grep -q '^foreign replacement$' "$mutant_root/source" ||
    fail "pathname mutation changed the foreign replacement"
grep -q '^foreign replacement$' "$mutant_root/alias" ||
    fail "pathname mutation did not link the substituted source"
[ "$(ls -di "$mutant_root/source" | awk '{print $1}')" = \
    "$(ls -di "$mutant_root/alias" | awk '{print $1}')" ] ||
    fail "pathname mutation did not publish the replacement inode"

echo "tar hard-link identity: pathname mutation rejected"
echo "tar hard-link identity: PASS"
