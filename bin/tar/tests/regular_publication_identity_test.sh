#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1
export ASAN_OPTIONS UBSAN_OPTIONS

source_directory=$(cd "$(dirname "$0")/.." && pwd)
work_directory=$(mktemp -d "${TMPDIR:-/tmp}/tar-regular-publication.XXXXXX")
trap 'rm -rf "$work_directory"' EXIT INT HUP TERM

fail() {
    echo "tar regular publication: FAIL: $*" >&2
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
"$work_directory/tarfixture" regular-publication > \
    "$work_directory/publication.tar"
"$work_directory/tarfixture" regular-replacement > \
    "$work_directory/replacement.tar"
"$work_directory/tarfixture" regular-replacement-write-only > \
    "$work_directory/replacement-write-only.tar"
"$work_directory/tarfixture" regular-replacement-zero-mode > \
    "$work_directory/replacement-zero-mode.tar"
"$work_directory/tarfixture" regular-truncated > \
    "$work_directory/truncated.tar"

prepare_collision_root() {
    collision_root=$1

    mkdir "$collision_root"
    "$PYTHON" - "$collision_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
for character in alphabet[:-1]:
    (root / ("00000" + character)).symlink_to("foreign")
PY
}

run_replacement_case() {
    archive=$1
    label=$2
    extraction_root=$work_directory/$label
    output=$work_directory/$label.out

    mkdir "$extraction_root"
    (
        cd "$extraction_root"
        TAR_REGULAR_TRACE=1 "$work_directory/tar" xf "$archive"
    ) >"$output" 2>&1 || return 1
    grep -q '^new payload$' "$extraction_root/victim" || return 1
    [ ! -e "$extraction_root/000000" ] || return 1
}

run_replacement_case "$work_directory/replacement-write-only.tar" \
    write-only-replacement ||
    fail "write-only repeated member was not replaced"
run_replacement_case "$work_directory/replacement-zero-mode.tar" \
    zero-mode-replacement ||
    fail "mode-zero repeated member was not replaced"

run_read_failure_case() {
    binary=$1
    read_error=$2
    expected_status=$3
    label=$4
    extraction_root=$work_directory/$label
    output=$work_directory/$label.out

    mkdir "$extraction_root"
    set +e
    (
        cd "$extraction_root"
        if [ "$read_error" -eq 1 ]; then
            TAR_REGULAR_READ_ERROR=1
            export TAR_REGULAR_READ_ERROR
        else
            unset TAR_REGULAR_READ_ERROR
        fi
        TAR_REGULAR_TRACE=1 "$binary" xf "$work_directory/truncated.tar"
    ) >"$output" 2>&1
    status=$?
    set -e
    [ "$status" -eq "$expected_status" ] ||
        fail "$label returned status $status instead of $expected_status"
    grep -F "tar regular publication: rooted cleanup" "$output" >/dev/null ||
        fail "$label missed descriptor-conditioned cleanup"
    [ ! -e "$extraction_root/000000" ] ||
        fail "$label retained the staged output"
    [ ! -e "$extraction_root/victim" ] ||
        fail "$label published the final target"
}

run_read_failure_case "$work_directory/tar" 0 2 truncated-payload ||
    fail "truncated payload retained a staged output"
run_read_failure_case "$work_directory/tar" 1 3 injected-read-error ||
    fail "archive read error retained a staged output"

write_failure_root=$work_directory/injected-write-error
write_failure_output=$work_directory/injected-write-error.out
mkdir "$write_failure_root"
set +e
(
    cd "$write_failure_root"
    TAR_REGULAR_TRACE=1 TAR_REGULAR_WRITE_ERROR=1         "$work_directory/tar" xf "$work_directory/publication.tar"
) >"$write_failure_output" 2>&1
write_failure_status=$?
set -e
[ "$write_failure_status" -eq 2 ] ||
    fail "output write error returned an unexpected status"
grep -F "tar: write output victim:" "$write_failure_output" >/dev/null ||
    fail "output write error missed its primary diagnostic"
grep -F "tar regular publication: rooted cleanup"     "$write_failure_output" >/dev/null ||
    fail "output write error missed descriptor-conditioned cleanup"
[ ! -e "$write_failure_root/000000" ] ||
    fail "output write error retained a staged output"
[ ! -e "$write_failure_root/victim" ] ||
    fail "output write error published a final target"

collision_root=$work_directory/staging-collisions
prepare_collision_root "$collision_root"
(
    cd "$collision_root"
    TAR_REGULAR_TRACE=1 "$work_directory/tar" xf \
        "$work_directory/publication.tar"
) >"$work_directory/staging-collisions.out" 2>&1 ||
    fail "bounded staging search rejected an available candidate"
grep -q '^new payload$' "$collision_root/victim" ||
    fail "bounded staging search lost the archive payload"
"$PYTHON" - "$collision_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
for character in alphabet[:-1]:
    candidate = root / ("00000" + character)
    if not candidate.is_symlink() or candidate.readlink() != Path("foreign"):
        raise SystemExit(f"staging collision changed {candidate.name}")
if (root / "00000z").exists() or (root / "00000z").is_symlink():
    raise SystemExit("published staging candidate remains named")
PY

run_rejection_case() {
    binary=$1
    archive=$2
    hook=$3
    label=$4
    extraction_root=$work_directory/$label
    output=$work_directory/$label.out

    mkdir "$extraction_root"
    set +e
    (
        cd "$extraction_root"
        unset TAR_REGULAR_CREATE_TARGET TAR_REGULAR_MOVED_STAGE \
            TAR_REGULAR_MOVED_TARGET
        case $hook in
        create-target)
            TAR_REGULAR_CREATE_TARGET=1
            export TAR_REGULAR_CREATE_TARGET
            ;;
        replace-stage)
            TAR_REGULAR_MOVED_STAGE=moved-stage
            export TAR_REGULAR_MOVED_STAGE
            ;;
        replace-target)
            TAR_REGULAR_MOVED_TARGET=moved-target
            export TAR_REGULAR_MOVED_TARGET
            ;;
        *)
            exit 97
            ;;
        esac
        TAR_REGULAR_TRACE=1 "$binary" xf "$archive"
    ) >"$output" 2>&1
    status=$?
    set -e
    [ "$status" -eq 2 ] || return 1
    grep -F "tar regular publication: rooted publish" "$output" >/dev/null ||
        return 1
    grep -F "tar regular publication: rooted cleanup" "$output" >/dev/null ||
        return 1
    grep -F "tar: publish output victim:" "$output" >/dev/null ||
        return 1
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:' \
        "$output" >/dev/null; then
        return 1
    fi
}

run_rejection_case "$work_directory/tar" \
    "$work_directory/publication.tar" create-target target-creation ||
    fail "competing absent-target creation reported success"
grep -q '^foreign replacement$' "$work_directory/target-creation/victim" ||
    fail "competing absent-target entry changed"
[ ! -e "$work_directory/target-creation/000000" ] ||
    fail "failed absent-target publication retained its stage"

run_rejection_case "$work_directory/tar" \
    "$work_directory/publication.tar" replace-stage stage-replacement ||
    fail "staged-entry replacement reported success"
grep -q '^new payload$' "$work_directory/stage-replacement/moved-stage" ||
    fail "moved descriptor-owned stage lost the archive payload"
grep -q '^foreign replacement$' "$work_directory/stage-replacement/000000" ||
    fail "foreign staged replacement changed"
[ ! -e "$work_directory/stage-replacement/victim" ] ||
    fail "staged-entry replacement published a final target"

run_rejection_case "$work_directory/tar" \
    "$work_directory/replacement.tar" replace-target target-replacement ||
    fail "competing regular-target replacement reported success"
grep -q '^old payload$' "$work_directory/target-replacement/moved-target" ||
    fail "moved extraction-owned target changed"
grep -q '^foreign replacement$' "$work_directory/target-replacement/victim" ||
    fail "foreign target replacement changed"
[ ! -e "$work_directory/target-replacement/000000" ] ||
    fail "failed target replacement retained its stage"

"$PYTHON" - "$source_directory/tar.c" \
    "$work_directory/tar_pathname_mutant.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = """publishat_beneath(extraction_root_descriptor,
                output_staging_path,"""
new = """tar_test_path_publish(extraction_root_descriptor,
                output_staging_path,"""
if source.count(old) != 1:
    raise SystemExit("regular publication mutation anchor count differs")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
$compiler $common_flags -o "$work_directory/tar_pathname_mutant" \
    "$work_directory/tar_pathname_mutant.c" \
    "$source_directory/tests/rooted_link_host.c" ||
    fail "compile pathname publication mutation"

mutant_root=$work_directory/pathname-mutant
mutant_output=$work_directory/pathname-mutant.out
mkdir "$mutant_root"
if ! (
    cd "$mutant_root"
    TAR_REGULAR_TRACE=1 TAR_REGULAR_MOVED_TARGET=moved-target \
        "$work_directory/tar_pathname_mutant" xf \
        "$work_directory/replacement.tar"
) >"$mutant_output" 2>&1; then
    cat "$mutant_output" >&2
    fail "pathname mutation did not reproduce target overwrite"
fi
grep -F "tar regular publication: pathname publish" \
    "$mutant_output" >/dev/null ||
    fail "pathname mutation did not reach its scheduled hook"
grep -q '^old payload$' "$mutant_root/moved-target" ||
    fail "pathname mutation lost the moved extraction-owned target"
grep -q '^new payload$' "$mutant_root/victim" ||
    fail "pathname mutation did not overwrite the foreign replacement"

"$PYTHON" - "$source_directory/tar.c" \
    "$work_directory/tar_single_stage_mutant.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = "for (attempt = 0; attempt < STAGING_ATTEMPTS; attempt++)"
new = "for (attempt = 0; attempt < 1U; attempt++)"
if source.count(old) != 1:
    raise SystemExit("staging search mutation anchor count differs")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
$compiler $common_flags -o "$work_directory/tar_single_stage_mutant" \
    "$work_directory/tar_single_stage_mutant.c" \
    "$source_directory/tests/rooted_link_host.c" ||
    fail "compile single-stage mutation"

single_stage_root=$work_directory/single-stage-mutant
prepare_collision_root "$single_stage_root"
single_stage_output=$work_directory/single-stage-mutant.out
set +e
(
    cd "$single_stage_root"
    TAR_REGULAR_TRACE=1 "$work_directory/tar_single_stage_mutant" xf \
        "$work_directory/publication.tar"
) >"$single_stage_output" 2>&1
single_stage_status=$?
set -e
[ "$single_stage_status" -eq 2 ] ||
    fail "single-stage mutation did not reject the collision corpus"
grep -F "tar: allocate staged output victim:" \
    "$single_stage_output" >/dev/null ||
    fail "single-stage mutation failed outside staged allocation"
[ ! -e "$single_stage_root/victim" ] ||
    fail "single-stage mutation published the final target"

"$PYTHON" - "$source_directory/tar.c" \
    "$work_directory/tar_no_exit_cleanup_mutant.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = """    discard_staged_output();
    filter_result = zreap(n == 0);"""
new = """    (void)discard_staged_output;
    filter_result = zreap(n == 0);"""
if source.count(old) != 1:
    raise SystemExit("exit cleanup mutation anchor count differs")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
$compiler $common_flags -o "$work_directory/tar_no_exit_cleanup_mutant" \
    "$work_directory/tar_no_exit_cleanup_mutant.c" \
    "$source_directory/tests/rooted_link_host.c" ||
    fail "compile missing exit-cleanup mutation"

exit_cleanup_root=$work_directory/exit-cleanup-mutant
exit_cleanup_output=$work_directory/exit-cleanup-mutant.out
mkdir "$exit_cleanup_root"
set +e
(
    cd "$exit_cleanup_root"
    TAR_REGULAR_TRACE=1 "$work_directory/tar_no_exit_cleanup_mutant" xf \
        "$work_directory/truncated.tar"
) >"$exit_cleanup_output" 2>&1
exit_cleanup_status=$?
set -e
[ "$exit_cleanup_status" -eq 2 ] ||
    fail "missing exit-cleanup mutation did not reach truncated input"
[ -f "$exit_cleanup_root/000000" ] ||
    fail "missing exit-cleanup mutation did not retain the staged output"
[ ! -e "$exit_cleanup_root/victim" ] ||
    fail "missing exit-cleanup mutation published the final target"

echo "tar regular publication: pathname mutation rejected"
echo "tar regular publication: staging search mutation rejected"
echo "tar regular publication: exit cleanup mutation rejected"
echo "tar regular publication: PASS"
