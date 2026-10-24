#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"

source_directory=$(cd "$(dirname "$0")/.." && pwd)
work_directory=$(mktemp -d "${TMPDIR:-/tmp}/cpio-cleanup-identity.XXXXXX")
trap 'rm -rf "$work_directory"' EXIT INT HUP TERM

fail() {
	echo "cpio cleanup identity: FAIL: $*" >&2
	exit 1
}

compiler=${CC:-cc}
common_flags="-D_DEFAULT_SOURCE -DCPIO_PATH_LIMIT=256 -std=c17 -O1 \
    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
    -Wold-style-definition -Wconversion -Wsign-conversion \
    -fno-omit-frame-pointer -fsanitize=address,undefined"
shim_header=$source_directory/tests/cleanup_identity_shim.h
shim_source=$source_directory/tests/cleanup_identity_shim.c

build_fixture() {
	source_file=$1
	output_file=$2
	object_prefix=$3

	$compiler $common_flags -include "$shim_header" \
	    -Dclose=cpio_test_close -Dwrite=cpio_test_write \
	    -c -o "$object_prefix-cpio.o" \
	    "$source_file" || fail "compile $output_file cpio source"
	$compiler $common_flags -c -o "$object_prefix-shim.o" \
	    "$shim_source" || fail "compile $output_file cleanup shim"
	$compiler $common_flags -o "$output_file" "$object_prefix-cpio.o" \
	    "$object_prefix-shim.o" || fail "link $output_file"
}

write_archive_prefix() {
	archive_file=$1
	entry_name=$2
	entry_size=$3
	{
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 0100644 0 0 1 0 0 $((${#entry_name} + 1)) \
		    "$entry_size"
		printf '%s\000' "$entry_name"
	} > "$archive_file"
}

append_archive_trailer() {
	{
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 0 0 0 1 0 0 11 0
		printf 'TRAILER!!!\000'
	} >> "$1"
}

run_rejected_extraction() {
	binary=$1
	case_directory=$2
	archive_file=$3
	shift 3

	if (cd "$case_directory" && "$@" "$binary" -id < "$archive_file" \
	    > stdout 2> stderr); then
		fail "$(basename "$case_directory") reported success"
	fi
}

candidate=$work_directory/cpio
build_fixture "$source_directory/cpio.c" "$candidate" \
    "$work_directory/candidate"

write_archive_prefix "$work_directory/truncated.odc" victim 8
printf 'short' >> "$work_directory/truncated.odc"
mkdir "$work_directory/truncated"
run_rejected_extraction "$candidate" "$work_directory/truncated" \
    "$work_directory/truncated.odc" env
[ ! -e "$work_directory/truncated/victim" ] ||
    fail "truncated input retained its incomplete output"
grep -F "truncated file data" "$work_directory/truncated/stderr" >/dev/null ||
    fail "truncated input lost its primary diagnostic"

write_archive_prefix "$work_directory/write-failure.odc" victim 7
printf 'payload' >> "$work_directory/write-failure.odc"
mkdir "$work_directory/write-failure"
run_rejected_extraction "$candidate" "$work_directory/write-failure" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1
[ ! -e "$work_directory/write-failure/victim" ] ||
    fail "write failure retained its incomplete output"
grep -F "write output victim" "$work_directory/write-failure/stderr" \
    >/dev/null || fail "write failure lost its primary diagnostic"

write_archive_prefix "$work_directory/close-failure.odc" victim 7
printf 'payload' >> "$work_directory/close-failure.odc"
append_archive_trailer "$work_directory/close-failure.odc"
mkdir "$work_directory/close-failure"
run_rejected_extraction "$candidate" "$work_directory/close-failure" \
    "$work_directory/close-failure.odc" env CPIO_TEST_FAIL_CLOSE=1
[ "$(cat "$work_directory/close-failure/victim")" = payload ] ||
    fail "close failure changed the completed output"
grep -F "close output victim" "$work_directory/close-failure/stderr" \
    >/dev/null || fail "close failure lost its primary diagnostic"

mkdir "$work_directory/replacement"
printf 'replacement\n' > "$work_directory/replacement/prepared"
run_rejected_extraction "$candidate" "$work_directory/replacement" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1 \
    CPIO_TEST_REPLACEMENT=prepared CPIO_TEST_TARGET=victim
[ "$(cat "$work_directory/replacement/victim")" = replacement ] ||
    fail "identity mismatch changed the replacement"
grep -F "cleanup remove victim" "$work_directory/replacement/stderr" \
    >/dev/null || fail "identity mismatch lacked a cleanup diagnostic"
grep -F "write output victim" "$work_directory/replacement/stderr" \
    >/dev/null || fail "identity mismatch replaced the primary diagnostic"

"$PYTHON" - "$source_directory/cpio.c" \
    "$work_directory/invalid-expected.c" \
    "$work_directory/pathname-remove.c" \
    "$work_directory/close-unlink.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
call = "unlinkat_beneath(root_descriptor, path, output_descriptor)"
if source.count(call) != 1:
    raise SystemExit("cleanup call mutation anchor count differs")
Path(sys.argv[2]).write_text(
    source.replace(call, "unlinkat_beneath(root_descriptor, path, -1)"),
    encoding="utf-8",
)
helper_start = "{\n    int remove_error = 0;\n    int close_error = 0;"
if source.count(helper_start) != 1:
    raise SystemExit("cleanup helper mutation anchor count differs")
pathname_source = source.replace(call, "unlink(path)")
pathname_source = pathname_source.replace(
    helper_start,
    "{\n    (void)root_descriptor;\n    int remove_error = 0;\n"
    "    int close_error = 0;",
)
Path(sys.argv[3]).write_text(pathname_source, encoding="utf-8")
close_path = """        if (output_descriptor >= 0 && close(output_descriptor) < 0)
            fatal_path("close output", archive_path);"""
if source.count(close_path) != 1:
    raise SystemExit("close cleanup mutation anchor count differs")
Path(sys.argv[4]).write_text(
    source.replace(
        close_path,
        """        if (output_descriptor >= 0 && close(output_descriptor) < 0) {
            (void)unlink(archive_path);
            fatal_path("close output", archive_path);
        }""",
    ),
    encoding="utf-8",
)
PY

invalid_expected=$work_directory/cpio-invalid-expected
build_fixture "$work_directory/invalid-expected.c" "$invalid_expected" \
    "$work_directory/invalid-expected"
mkdir "$work_directory/invalid-expected-case"
run_rejected_extraction "$invalid_expected" \
    "$work_directory/invalid-expected-case" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1
[ -f "$work_directory/invalid-expected-case/victim" ] ||
    fail "invalid-expected mutation did not retain the incomplete output"
echo "cpio cleanup identity: invalid-expected mutation rejected"

pathname_remove=$work_directory/cpio-pathname-remove
build_fixture "$work_directory/pathname-remove.c" "$pathname_remove" \
    "$work_directory/pathname-remove"
mkdir "$work_directory/pathname-remove-case"
printf 'replacement\n' > "$work_directory/pathname-remove-case/prepared"
run_rejected_extraction "$pathname_remove" \
    "$work_directory/pathname-remove-case" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1 \
    CPIO_TEST_REPLACEMENT=prepared CPIO_TEST_TARGET=victim
[ ! -e "$work_directory/pathname-remove-case/victim" ] ||
    fail "pathname-remove mutation preserved the replacement"
echo "cpio cleanup identity: pathname-remove mutation rejected"

close_unlink=$work_directory/cpio-close-unlink
build_fixture "$work_directory/close-unlink.c" "$close_unlink" \
    "$work_directory/close-unlink"
mkdir "$work_directory/close-unlink-case"
run_rejected_extraction "$close_unlink" \
    "$work_directory/close-unlink-case" \
    "$work_directory/close-failure.odc" env CPIO_TEST_FAIL_CLOSE=1
[ ! -e "$work_directory/close-unlink-case/victim" ] ||
    fail "close-unlink mutation preserved the completed output"
echo "cpio cleanup identity: close-unlink mutation rejected"
echo "cpio cleanup identity: PASS"
