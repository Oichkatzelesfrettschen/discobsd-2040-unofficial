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
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS
common_flags="-D_DEFAULT_SOURCE -DCPIO_PATH_LIMIT=256 -std=c17 -O1 \
    -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes \
    -Wold-style-definition -Wconversion -Wsign-conversion \
	    -fno-omit-frame-pointer -fsanitize=address,undefined \
	    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3"
interposition_header=$source_directory/tests/cleanup_identity_interpose.h
shim_header=$source_directory/tests/cleanup_identity_shim.h
shim_source=$source_directory/tests/cleanup_identity_shim.c

build_fixture() {
	source_file=$1
	output_file=$2
	object_prefix=$3
	fixture_header=${4:-$interposition_header}

	$compiler $common_flags -include "$fixture_header" \
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
	entry_mode=${4:-0100644}
	{
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 "$entry_mode" 0 0 1 0 0 $((${#entry_name} + 1)) \
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

write_archive_prefix "$work_directory/directory-identity.odc" victim 0 \
    040000
append_archive_trailer "$work_directory/directory-identity.odc"
mkdir "$work_directory/directory-identity"
mkdir "$work_directory/directory-identity/replacement"
chmod 0755 "$work_directory/directory-identity/replacement"
directory_identity_trace=$work_directory/directory-identity-trace
(cd "$work_directory/directory-identity" && env \
    CPIO_TEST_REPLACE_DIRECTORY_VERIFY=1 \
    CPIO_TEST_DIRECTORY_REPLACEMENT=replacement \
    CPIO_TEST_DIRECTORY_SAVED_PATH=created \
    CPIO_TEST_TRACE="$directory_identity_trace" \
    "$candidate" -id < "$work_directory/directory-identity.odc" \
    > stdout 2> stderr) || fail "directory identity extraction failed"
directory_replacement_mode=$("$PYTHON" - \
    "$work_directory/directory-identity/victim" <<'PY'
from pathlib import Path
import stat
import sys

print(f"{stat.S_IMODE(Path(sys.argv[1]).stat().st_mode):04o}")
PY
)
[ "$directory_replacement_mode" = 0755 ] ||
    fail "directory replacement received created-directory mode"
[ -d "$work_directory/directory-identity/created" ] ||
    fail "created directory identity was not retained by the fixture"
grep -Fx "replace_directory_verify" "$directory_identity_trace" >/dev/null ||
    fail "directory identity replacement hook was bypassed"

write_archive_prefix "$work_directory/stage-symlink.odc" victim 7
printf 'payload' >> "$work_directory/stage-symlink.odc"
append_archive_trailer "$work_directory/stage-symlink.odc"
mkdir "$work_directory/stage-symlink"
ln -s foreign "$work_directory/stage-symlink/000000"
stage_symlink_trace=$work_directory/stage-symlink-trace
(cd "$work_directory/stage-symlink" && env \
    CPIO_TEST_STAGE_SYMLINK_ELOOP=1 \
    CPIO_TEST_TRACE="$stage_symlink_trace" \
    "$candidate" -id < "$work_directory/stage-symlink.odc" \
    > stdout 2> stderr) || fail "symlink-occupied staging retry failed"
[ "$(cat "$work_directory/stage-symlink/victim")" = payload ] ||
    fail "symlink-occupied staging retry lost archive bytes"
[ -L "$work_directory/stage-symlink/000000" ] ||
    fail "symlink-occupied staging retry changed the foreign symlink"
grep -Fx "stage_symlink_eloop" "$stage_symlink_trace" >/dev/null ||
    fail "staging symlink ELOOP hook was bypassed"

bypass_header=$work_directory/cleanup-identity-interpose-bypass.h
"$PYTHON" - "$interposition_header" "$bypass_header" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
anchor = "#define openat_beneath cpio_test_openat_beneath\n"
if source.count(anchor) != 1:
    raise SystemExit("openat_beneath interposition anchor count differs")
Path(sys.argv[2]).write_text(source.replace(anchor, ""), encoding="utf-8")
PY
bypass_candidate=$work_directory/cpio-interposition-bypass
build_fixture "$source_directory/cpio.c" "$bypass_candidate" \
    "$work_directory/interposition-bypass" "$bypass_header"

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
replacement_stage_path=$work_directory/replacement-stage-path
replacement_trace=$work_directory/replacement-trace
run_rejected_extraction "$candidate" "$work_directory/replacement" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1 \
    CPIO_TEST_REPLACE_STAGE_CLEANUP=1 \
    CPIO_TEST_STAGE_SAVED_PATH=saved \
    CPIO_TEST_STAGE_PATH_FILE="$replacement_stage_path" \
    CPIO_TEST_TRACE="$replacement_trace"
[ -s "$replacement_stage_path" ] ||
    fail "stage-cleanup replacement did not reach rooted creation"
replacement_stage=$(cat "$replacement_stage_path")
grep -Fx "replace_staged_entry" "$replacement_trace" >/dev/null ||
    fail "stage-cleanup replacement hook was bypassed"
[ "$(cat "$work_directory/replacement/$replacement_stage")" = \
    "foreign staged replacement" ] ||
    fail "identity mismatch changed the staged replacement"
[ -f "$work_directory/replacement/saved" ] ||
    fail "identity mismatch lost the descriptor-owned staged inode"
[ ! -e "$work_directory/replacement/victim" ] ||
    fail "failed extraction published its target"
grep -F "cleanup remove $replacement_stage" \
    "$work_directory/replacement/stderr" >/dev/null ||
    fail "identity mismatch lacked a cleanup diagnostic"
grep -F "write output victim" "$work_directory/replacement/stderr" \
    >/dev/null || fail "identity mismatch replaced the primary diagnostic"

write_archive_prefix "$work_directory/publication.odc" victim 7
printf 'payload' >> "$work_directory/publication.odc"
append_archive_trailer "$work_directory/publication.odc"

mkdir "$work_directory/target-replacement"
printf 'foreign target\n' > "$work_directory/target-replacement/prepared"
target_trace=$work_directory/target-replacement-trace
run_rejected_extraction "$candidate" \
    "$work_directory/target-replacement" \
    "$work_directory/publication.odc" env \
    CPIO_TEST_REPLACE_TARGET_PUBLICATION=prepared \
    CPIO_TEST_TRACE="$target_trace"
grep -Fx "replace_target_publication" "$target_trace" >/dev/null ||
    fail "target-replacement publication hook was bypassed"
[ "$(cat "$work_directory/target-replacement/victim")" = \
    "foreign target" ] || fail "publication changed the foreign target"
grep -F "publish output victim" \
    "$work_directory/target-replacement/stderr" >/dev/null ||
    fail "target replacement lacked the publication diagnostic"

mkdir "$work_directory/stage-replacement"
stage_publication_path=$work_directory/stage-publication-path
stage_publication_trace=$work_directory/stage-publication-trace
run_rejected_extraction "$candidate" \
    "$work_directory/stage-replacement" \
    "$work_directory/publication.odc" env \
    CPIO_TEST_REPLACE_STAGE_PUBLICATION=1 \
    CPIO_TEST_STAGE_SAVED_PATH=saved \
    CPIO_TEST_STAGE_PATH_FILE="$stage_publication_path" \
    CPIO_TEST_TRACE="$stage_publication_trace"
publication_stage=$(cat "$stage_publication_path")
grep -Fx "replace_staged_entry" "$stage_publication_trace" >/dev/null ||
    fail "stage-replacement publication hook was bypassed"
[ "$(cat "$work_directory/stage-replacement/$publication_stage")" = \
    "foreign staged replacement" ] ||
    fail "publication changed the foreign staged replacement"
[ "$(cat "$work_directory/stage-replacement/saved")" = payload ] ||
    fail "publication replacement changed the descriptor-owned payload"
[ ! -e "$work_directory/stage-replacement/victim" ] ||
    fail "stage replacement published a target"
grep -F "publish output victim" \
    "$work_directory/stage-replacement/stderr" >/dev/null ||
    fail "stage replacement lacked the publication diagnostic"

make_single_directory_archive() {
	archive_file=$1
	entry_name=$2
	{
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 040755 0 0 1 0 0 $((${#entry_name} + 1)) 0
		printf '%s\000' "$entry_name"
		printf '070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' \
		    0 0 0 0 0 1 0 0 11 0
		printf 'TRAILER!!!\000'
	} > "$archive_file"
}

make_single_directory_archive "$work_directory/directory-relocation.odc" \
    inside/newdir
mkdir -p "$work_directory/directory-relocation/inside"
directory_trace=$work_directory/directory-relocation-trace
(cd "$work_directory/directory-relocation" && env \
    CPIO_TEST_RELOCATE_BEFORE_DIRECTORY=1 \
    CPIO_TEST_RELOCATE_ANCESTOR=inside \
    CPIO_TEST_RELOCATE_DESTINATION=moved \
    CPIO_TEST_TRACE="$directory_trace" \
    "$candidate" -id < "$work_directory/directory-relocation.odc" \
    > stdout 2> stderr) || fail "directory relocation extraction failed"
grep -Fx "relocate_ancestor" "$directory_trace" >/dev/null ||
    fail "directory relocation hook was bypassed"
[ ! -e "$work_directory/directory-relocation/moved/newdir" ] ||
    fail "directory creation escaped through the relocated ancestor"
[ -d "$work_directory/directory-relocation/inside/newdir" ] ||
    fail "directory relocation omitted the rooted directory"

write_archive_prefix "$work_directory/create-relocation.odc" inside/victim 7
printf 'payload' >> "$work_directory/create-relocation.odc"
append_archive_trailer "$work_directory/create-relocation.odc"
mkdir -p "$work_directory/create-relocation/inside"
create_trace=$work_directory/create-relocation-trace
run_rejected_extraction "$candidate" "$work_directory/create-relocation" \
    "$work_directory/create-relocation.odc" env \
    CPIO_TEST_RELOCATE_BEFORE_CREATE=1 \
    CPIO_TEST_RELOCATE_ANCESTOR=inside \
    CPIO_TEST_RELOCATE_DESTINATION=moved \
    CPIO_TEST_TRACE="$create_trace"
grep -Fx "relocate_ancestor" "$create_trace" >/dev/null ||
    fail "staged-creation relocation hook was bypassed"
[ ! -e "$work_directory/create-relocation/moved/victim" ] ||
    fail "staged creation escaped through the relocated ancestor"
[ -z "$(find "$work_directory/create-relocation/moved" -mindepth 1 -print -quit)" ] ||
    fail "staged creation changed the relocated ancestor"

mkdir -p "$work_directory/publication-relocation/inside"
publication_relocation_path=$work_directory/publication-relocation-stage
publication_relocation_trace=$work_directory/publication-relocation-trace
run_rejected_extraction "$candidate" \
    "$work_directory/publication-relocation" \
    "$work_directory/create-relocation.odc" env \
    CPIO_TEST_RELOCATE_BEFORE_PUBLICATION=1 \
    CPIO_TEST_RELOCATE_ANCESTOR=inside \
    CPIO_TEST_RELOCATE_DESTINATION=moved \
    CPIO_TEST_STAGE_PATH_FILE="$publication_relocation_path" \
    CPIO_TEST_TRACE="$publication_relocation_trace"
relocated_stage=$(cat "$publication_relocation_path")
grep -Fx "relocate_ancestor" "$publication_relocation_trace" >/dev/null ||
    fail "publication relocation hook was bypassed"
[ "$(cat "$work_directory/publication-relocation/moved/${relocated_stage#inside/}")" = \
    payload ] || fail "relocated staged inode lost its archive payload"
[ ! -e "$work_directory/publication-relocation/moved/victim" ] ||
    fail "relocated ancestor received the final target"
grep -F "publish output inside/victim" \
    "$work_directory/publication-relocation/stderr" >/dev/null ||
    fail "ancestor relocation lacked the publication diagnostic"

mkdir -p "$work_directory/interposition-bypass-case/inside"
bypass_trace=$work_directory/interposition-bypass-trace
if ! (cd "$work_directory/interposition-bypass-case" && env \
    CPIO_TEST_RELOCATE_BEFORE_CREATE=1 \
    CPIO_TEST_RELOCATE_ANCESTOR=inside \
    CPIO_TEST_RELOCATE_DESTINATION=moved \
    CPIO_TEST_TRACE="$bypass_trace" \
    "$bypass_candidate" -id < "$work_directory/create-relocation.odc" \
    > stdout 2> stderr); then
	cat "$work_directory/interposition-bypass-case/stderr" >&2
	fail "interposition bypass mutation did not reproduce false success"
fi
if grep -Fx "relocate_ancestor" "$bypass_trace" >/dev/null 2>&1; then
	fail "interposition bypass mutation reached the relocation hook"
fi
[ "$(cat "$work_directory/interposition-bypass-case/inside/victim")" = \
    payload ] || fail "interposition bypass mutation lost its payload"

"$PYTHON" - "$source_directory/cpio.c" \
    "$work_directory/invalid-expected.c" \
    "$work_directory/pathname-remove.c" \
    "$work_directory/close-unlink.c" \
    "$work_directory/direct-final-name.c" \
    "$work_directory/cwd-parent-traversal.c" \
    "$work_directory/chmod-reacquired-directory.c" \
    "$work_directory/abort-stage-symlink.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
call = "unlinkat_beneath(root_descriptor, io_buffer, output_descriptor)"
if source.count(call) != 1:
    raise SystemExit("cleanup call mutation anchor count differs")
Path(sys.argv[2]).write_text(
    source.replace(call, "unlinkat_beneath(root_descriptor, io_buffer, -1)"),
    encoding="utf-8",
)
helper_start = "{\n    int remove_error = 0;\n    int close_error = 0;"
if source.count(helper_start) != 1:
    raise SystemExit("cleanup helper mutation anchor count differs")
pathname_source = source.replace(call, "unlink(io_buffer)")
pathname_source = pathname_source.replace(
    helper_start,
    "{\n    int remove_error = 0;\n    int close_error = 0;\n\n"
    "    (void)root_descriptor;",
)
Path(sys.argv[3]).write_text(pathname_source, encoding="utf-8")
close_path = """            if (close(output_descriptor) < 0)
                fatal_path("close output", archive_path);"""
if source.count(close_path) != 1:
    raise SystemExit("close cleanup mutation anchor count differs")
Path(sys.argv[4]).write_text(
    source.replace(
        close_path,
        """            if (close(output_descriptor) < 0) {
                (void)unlink(archive_path);
                fatal_path("close output", archive_path);
            }""",
    ),
    encoding="utf-8",
)

creation_call = """                output_descriptor = create_staged_output(root_descriptor,
                    (mode_t)mode_value, &staging_attempt);"""
if source.count(creation_call) != 1:
    raise SystemExit("staged creation mutation anchor count differs")
direct_creation = """                if (0)
                    output_descriptor = create_staged_output(root_descriptor,
                        (mode_t)mode_value, &staging_attempt);
                output_descriptor = openat_beneath(root_descriptor,
                    archive_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
                    (mode_t)mode_value & 0777);"""
Path(sys.argv[5]).write_text(
    source.replace(creation_call, direct_creation), encoding="utf-8"
)

parent_call = "            create_parent_directories(root_descriptor);"
descriptor_declaration = "        int output_descriptor = -1;"
if source.count(parent_call) != 1:
    raise SystemExit("rooted parent traversal mutation anchor count differs")
if source.count(descriptor_declaration) != 1:
    raise SystemExit("leaf declaration mutation anchor count differs")
cwd_source = source.replace(
    descriptor_declaration,
    "        char *leaf_name;\n        int output_descriptor = -1;",
)
cwd_source = cwd_source.replace(
    parent_call,
    """            if (0)
                create_parent_directories(root_descriptor);
            leaf_name = enter_parent_directories(root_descriptor, 1);""",
)
cwd_creation = """                if (0)
                    output_descriptor = create_staged_output(root_descriptor,
                        (mode_t)mode_value, &staging_attempt);
                output_descriptor = open(leaf_name,
                    O_WRONLY | O_CREAT | O_EXCL,
                    (mode_t)mode_value & 0777);"""
cwd_source = cwd_source.replace(creation_call, cwd_creation)
Path(sys.argv[6]).write_text(cwd_source, encoding="utf-8")

directory_close = """    if (close(directory_descriptor) < 0)
        fatal_path("close directory", archive_path);"""
if source.count(directory_close) != 1:
    raise SystemExit("directory close mutation anchor count differs")
Path(sys.argv[7]).write_text(
    source.replace(
        directory_close,
        """    if (fchmod(directory_descriptor, directory_mode) < 0)
        fatal_path("set created directory mode", archive_path);
    if (close(directory_descriptor) < 0)
        fatal_path("close directory", archive_path);""",
    ),
    encoding="utf-8",
)

occupied_stage = "        if (errno != EEXIST && errno != ELOOP)"
if source.count(occupied_stage) != 1:
    raise SystemExit("occupied staging mutation anchor count differs")
Path(sys.argv[8]).write_text(
    source.replace(occupied_stage, "        if (errno != EEXIST)"),
    encoding="utf-8",
)
PY

invalid_expected=$work_directory/cpio-invalid-expected
build_fixture "$work_directory/invalid-expected.c" "$invalid_expected" \
    "$work_directory/invalid-expected"
mkdir "$work_directory/invalid-expected-case"
invalid_stage_path=$work_directory/invalid-expected-stage
run_rejected_extraction "$invalid_expected" \
    "$work_directory/invalid-expected-case" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1 \
    CPIO_TEST_STAGE_PATH_FILE="$invalid_stage_path"
invalid_stage=$(cat "$invalid_stage_path")
[ -f "$work_directory/invalid-expected-case/$invalid_stage" ] ||
    fail "invalid-expected mutation did not retain the incomplete output"
echo "cpio cleanup identity: invalid-expected mutation rejected"

pathname_remove=$work_directory/cpio-pathname-remove
build_fixture "$work_directory/pathname-remove.c" "$pathname_remove" \
    "$work_directory/pathname-remove"
mkdir "$work_directory/pathname-remove-case"
pathname_stage_path=$work_directory/pathname-remove-stage
run_rejected_extraction "$pathname_remove" \
    "$work_directory/pathname-remove-case" \
    "$work_directory/write-failure.odc" env CPIO_TEST_FAIL_WRITE=1 \
    CPIO_TEST_REPLACE_STAGE_CLEANUP=1 \
    CPIO_TEST_STAGE_SAVED_PATH=saved \
    CPIO_TEST_STAGE_PATH_FILE="$pathname_stage_path"
pathname_stage=$(cat "$pathname_stage_path")
[ ! -e "$work_directory/pathname-remove-case/$pathname_stage" ] ||
    fail "pathname-remove mutation preserved the replacement"
[ -f "$work_directory/pathname-remove-case/saved" ] ||
    fail "pathname-remove mutation lost the descriptor-owned staged inode"
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

direct_final=$work_directory/cpio-direct-final-name
build_fixture "$work_directory/direct-final-name.c" "$direct_final" \
    "$work_directory/direct-final-name"
mkdir "$work_directory/direct-final-name-case"
run_rejected_extraction "$direct_final" \
    "$work_directory/direct-final-name-case" \
    "$work_directory/publication.odc" env
[ "$(cat "$work_directory/direct-final-name-case/victim")" = payload ] ||
    fail "direct-final-name mutation did not expose its partial publication"
echo "cpio cleanup identity: direct-final-name mutation rejected"

cwd_parent=$work_directory/cpio-cwd-parent-traversal
build_fixture "$work_directory/cwd-parent-traversal.c" "$cwd_parent" \
    "$work_directory/cwd-parent-traversal"
mkdir -p "$work_directory/cwd-parent-case/inside"
cwd_source_path=$work_directory/cwd-parent-case/inside
cwd_destination_path=$work_directory/cwd-parent-case/moved
cwd_trace=$work_directory/cwd-parent-trace
run_rejected_extraction "$cwd_parent" "$work_directory/cwd-parent-case" \
    "$work_directory/create-relocation.odc" env \
    CPIO_TEST_LEGACY_RELOCATE_BEFORE_CREATE=1 \
    CPIO_TEST_LEGACY_ANCESTOR="$cwd_source_path" \
    CPIO_TEST_LEGACY_DESTINATION="$cwd_destination_path" \
    CPIO_TEST_TRACE="$cwd_trace"
grep -Fx "legacy_relocate_before_create" "$cwd_trace" >/dev/null ||
    fail "current-directory mutation bypassed its relocation hook"
[ "$(cat "$work_directory/cwd-parent-case/moved/victim")" = payload ] ||
    fail "current-directory mutation did not escape through the moved parent"
echo "cpio cleanup identity: current-directory mutation rejected"

chmod_reacquired=$work_directory/cpio-chmod-reacquired-directory
build_fixture "$work_directory/chmod-reacquired-directory.c" \
    "$chmod_reacquired" "$work_directory/chmod-reacquired-directory"
mkdir "$work_directory/chmod-reacquired-directory-case"
mkdir "$work_directory/chmod-reacquired-directory-case/replacement"
chmod 0755 "$work_directory/chmod-reacquired-directory-case/replacement"
(cd "$work_directory/chmod-reacquired-directory-case" && env \
    CPIO_TEST_REPLACE_DIRECTORY_VERIFY=1 \
    CPIO_TEST_DIRECTORY_REPLACEMENT=replacement \
    CPIO_TEST_DIRECTORY_SAVED_PATH=created \
    "$chmod_reacquired" -id < "$work_directory/directory-identity.odc" \
    > stdout 2> stderr) || fail "reacquired-directory mutation aborted"
chmod_reacquired_mode=$("$PYTHON" - \
    "$work_directory/chmod-reacquired-directory-case/victim" <<'PY'
from pathlib import Path
import stat
import sys

print(f"{stat.S_IMODE(Path(sys.argv[1]).stat().st_mode):04o}")
PY
)
[ "$chmod_reacquired_mode" = 0700 ] ||
    fail "reacquired-directory mutation did not change the foreign mode"
echo "cpio cleanup identity: chmod-reacquired-directory mutation rejected"

abort_stage_symlink=$work_directory/cpio-abort-stage-symlink
build_fixture "$work_directory/abort-stage-symlink.c" \
    "$abort_stage_symlink" "$work_directory/abort-stage-symlink"
mkdir "$work_directory/abort-stage-symlink-case"
ln -s foreign "$work_directory/abort-stage-symlink-case/000000"
run_rejected_extraction "$abort_stage_symlink" \
    "$work_directory/abort-stage-symlink-case" \
    "$work_directory/stage-symlink.odc" env \
    CPIO_TEST_STAGE_SYMLINK_ELOOP=1
[ ! -e "$work_directory/abort-stage-symlink-case/victim" ] ||
    fail "stage-symlink mutation unexpectedly published the target"
echo "cpio cleanup identity: abort-stage-symlink mutation rejected"
echo "cpio cleanup identity: PASS"
