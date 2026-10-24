#!/bin/sh
#
# Exercise the shipped 12-bit LZW configuration at host width.  The malformed
# stream is the CVE-2006-1168 reproducer with its header constrained to the
# target's BITS value; the decoder must reject its cyclic prefix chain before
# the overlaid htab storage ends.
#
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
LC_ALL=C
export LC_ALL

source_directory=$(cd "$(dirname "$0")/.." && pwd)
work_directory=$(mktemp -d "${TMPDIR:-/tmp}/compresscheck.XXXXXX")
trap 'rm -rf "$work_directory"' EXIT INT HUP TERM

fail()
{
	echo "compresscheck: FAIL: $*" >&2
	exit 1
}

compress_binary=$work_directory/compress
compiler=${CC:-cc}
host_search_flags=
if printf '%s\n' '#include <fcntl.h>' 'int value = O_SEARCH;' |
	$compiler -D_DEFAULT_SOURCE -std=c17 -pedantic-errors -Wall -Wextra \
	-Werror -x c -fsyntax-only - > /dev/null 2>&1; then
	:
elif printf '%s\n' '#include <fcntl.h>' 'int value = O_PATH;' |
	$compiler -D_GNU_SOURCE -std=c17 -pedantic-errors -Wall -Wextra \
	-Werror -x c -fsyntax-only - > /dev/null 2>&1; then
	host_search_flags='-D_GNU_SOURCE -DO_SEARCH=O_PATH'
else
	fail "host compiler provides neither O_SEARCH nor O_PATH"
fi
common_flags="-DUSERMEM=1024 -D_DEFAULT_SOURCE $host_search_flags \
-std=c17 -pedantic-errors -O1 -g -Wall -Wextra -Werror"
sanitizer_flags="-fno-omit-frame-pointer -fsanitize=address,undefined"
publication_shim=$source_directory/tests/publication_identity_shim.c
interposition_header=$source_directory/tests/publication_interpose.h
fortify_flags='-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3'

$compiler $common_flags $sanitizer_flags -o "$compress_binary" \
	"$source_directory/compress.c" "$publication_shim" ||
	fail "host sanitizer build"
$compiler $common_flags -DDEBUG -fsyntax-only \
	"$source_directory/compress.c" || fail "DEBUG syntax build"
$compiler $common_flags $fortify_flags $sanitizer_flags \
	-include "$interposition_header" -c \
	-o "$work_directory/compress-interposed.o" \
	"$source_directory/compress.c" || fail "interposed source build"
$compiler $common_flags $fortify_flags $sanitizer_flags -c \
	-o "$work_directory/publication-identity-shim.o" \
	"$publication_shim" || fail "publication shim build"
$compiler $sanitizer_flags -o "$work_directory/compress-interposed" \
	"$work_directory/compress-interposed.o" \
	"$work_directory/publication-identity-shim.o" ||
	fail "interposed transaction link"

bypass_header=$work_directory/publication-interpose-bypass.h
"$PYTHON" - "$interposition_header" "$bypass_header" <<'PY'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
mutated_path = Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")
anchor = "#define openat compress_test_openat\n"
if source.count(anchor) != 1:
    raise SystemExit("openat interposition mutation anchor count differs")
mutated_path.write_text(source.replace(anchor, ""), encoding="utf-8")
PY
$compiler $common_flags $fortify_flags $sanitizer_flags \
	-include "$bypass_header" -c \
	-o "$work_directory/compress-interposition-bypass.o" \
	"$source_directory/compress.c" || fail "interposition bypass mutation build"
$compiler $sanitizer_flags -o "$work_directory/compress-interposition-bypass" \
	"$work_directory/compress-interposition-bypass.o" \
	"$work_directory/publication-identity-shim.o" ||
	fail "interposition bypass mutation link"

input_file=$work_directory/input
iteration=0
while [ "$iteration" -lt 256 ]; do
	printf '%s\n' 'DiscoBSD RP2040 bounded LZW round trip' >> "$input_file"
	iteration=$((iteration + 1))
done

bypass_directory=$work_directory/interposition-bypass
bypass_trace=$bypass_directory/interposition.trace
mkdir "$bypass_directory"
cp "$input_file" "$bypass_directory/input"
printf '%s\n' 'replacement input' > "$bypass_directory/input.replacement"
bypass_status=0
COMPRESS_TEST_TRACE=$bypass_trace COMPRESS_TEST_REPLACE_INPUT=input \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$work_directory/compress-interposition-bypass" -f \
	"$bypass_directory/input" > "$bypass_directory/stdout" \
	2> "$bypass_directory/stderr" || bypass_status=$?
if [ "$bypass_status" -ne 0 ]; then
	fail "interposition bypass mutation did not reproduce false success"
fi
if grep -qx 'replace_input' "$bypass_trace"; then
	fail "interposition bypass mutation reached the replacement hook"
fi
[ ! -e "$bypass_directory/input" ] ||
	fail "interposition bypass mutation retained its input"
[ -f "$bypass_directory/input.Z" ] ||
	fail "interposition bypass mutation omitted its output"

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -c < "$input_file" > "$input_file.Z" ||
	fail "compress known-good input"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -dc < "$input_file.Z" > "$work_directory/roundtrip" ||
	fail "decompress known-good input"
cmp "$input_file" "$work_directory/roundtrip" || fail "round trip differs"
gzip -dc "$input_file.Z" > "$work_directory/gzip-roundtrip" ||
	fail "gzip rejected the generated archive"
cmp "$input_file" "$work_directory/gzip-roundtrip" ||
	fail "gzip round trip differs"

for option_value in 0 9 12 13 2147483647 2147483648 \
	999999999999999999999999999; do
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -b "$option_value" -c < "$input_file" \
		> "$work_directory/bits-$option_value.Z" ||
		fail "valid -b value $option_value was rejected"
done
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -b9 -c < "$input_file" \
	> "$work_directory/bits-attached.Z" ||
	fail "valid attached -b value was rejected"
"$PYTHON" - "$work_directory" <<'PY'
from pathlib import Path
import sys

work_directory = Path(sys.argv[1])
expected_headers = {
    "bits-0.Z": 0x89,
    "bits-9.Z": 0x89,
    "bits-12.Z": 0x8C,
    "bits-13.Z": 0x8C,
    "bits-2147483647.Z": 0x8C,
    "bits-2147483648.Z": 0x8C,
    "bits-999999999999999999999999999.Z": 0x8C,
    "bits-attached.Z": 0x89,
}
for filename, expected_header in expected_headers.items():
    archive = (work_directory / filename).read_bytes()
    if archive[:3] != bytes((0x1F, 0x9D, expected_header)):
        raise SystemExit(f"{filename}: unexpected LZW header")
PY

for option_value in 9junk +9 -9 ' 9' ''; do
	if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -b "$option_value" -c < "$input_file" \
		> "$work_directory/invalid-bits.stdout" \
		2> "$work_directory/invalid-bits.stderr"; then
		fail "invalid -b value '$option_value' was accepted"
	fi
	grep -q '^Maxbits must be an unsigned decimal integer$' \
		"$work_directory/invalid-bits.stderr" ||
		fail "invalid -b value '$option_value' lacks a grammar diagnostic"
done
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -b9junk -c < "$input_file" \
	> "$work_directory/invalid-attached.stdout" \
	2> "$work_directory/invalid-attached.stderr"; then
	fail "invalid attached -b value was accepted"
fi
grep -q '^Maxbits must be an unsigned decimal integer$' \
	"$work_directory/invalid-attached.stderr" ||
	fail "invalid attached -b value lacks a grammar diagnostic"

long_name_directory=$work_directory/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
mkdir "$long_name_directory"
long_name_input=$long_name_directory/input
long_name_archive=$long_name_directory/input.Z
cp "$input_file" "$long_name_input"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -c "$long_name_input" > "$long_name_archive" ||
	fail "compress stdout mode rejected a long input path"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -dc "$long_name_archive" \
	> "$work_directory/long-name-roundtrip" ||
	fail "decompress stdout mode rejected a long input path"
cmp "$input_file" "$work_directory/long-name-roundtrip" ||
	fail "long-path stdout round trip differs"

stdout_identity_directory=$work_directory/stdout-input-identity
mkdir "$stdout_identity_directory"
cp "$input_file" "$stdout_identity_directory/input"
ln "$stdout_identity_directory/input" \
	"$stdout_identity_directory/input-hardlink"
ln -s input "$stdout_identity_directory/input-symlink"
for stdout_input in input-hardlink input-symlink; do
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -c \
		"$stdout_identity_directory/$stdout_input" \
		> "$stdout_identity_directory/$stdout_input.Z" ||
		fail "stdout compression rejected $stdout_input"
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -dc \
		"$stdout_identity_directory/$stdout_input.Z" \
		> "$stdout_identity_directory/$stdout_input.roundtrip" ||
		fail "stdout decompression rejected $stdout_input archive"
	cmp "$input_file" \
		"$stdout_identity_directory/$stdout_input.roundtrip" ||
		fail "stdout $stdout_input round trip differs"
done
ln -s input-hardlink.Z "$stdout_identity_directory/archive-symlink.Z"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -dc \
	"$stdout_identity_directory/archive-symlink.Z" \
	> "$stdout_identity_directory/archive-symlink.roundtrip" ||
	fail "stdout decompression rejected a symbolic-link input"
cmp "$input_file" "$stdout_identity_directory/archive-symlink.roundtrip" ||
	fail "stdout symbolic-link archive round trip differs"
[ -L "$stdout_identity_directory/input-symlink" ] ||
	fail "stdout compression replaced its symbolic-link input"
[ -f "$stdout_identity_directory/input-hardlink" ] ||
	fail "stdout compression removed its multiply linked input"

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -f "$long_name_input" \
	> "$work_directory/long-name-named.stdout" \
	2> "$work_directory/long-name-named.stderr" ||
	fail "named compression rejected a long parent path"
[ ! -e "$long_name_input" ] ||
	fail "named long-path compression retained the input"
[ -f "$long_name_archive" ] ||
	fail "named long-path compression omitted the archive"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -d "$long_name_input" \
	> "$work_directory/long-name-named-decompress.stdout" \
	2> "$work_directory/long-name-named-decompress.stderr" ||
	fail "named decompression rejected a long parent path without a suffix"
cmp "$input_file" "$long_name_input" ||
	fail "named long-path round trip differs"

malformed_archive=$work_directory/malformed.Z
printf '\037\235\214\141\002\012\034\135\314\240\301\203\010\023\042\124\000' \
	> "$malformed_archive"

if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -dc < "$malformed_archive" \
	> "$work_directory/malformed.stdout" 2> "$work_directory/malformed.stderr"; then
	fail "malformed stdin reported success"
fi
grep -q '^uncompress: corrupt input$' "$work_directory/malformed.stderr" ||
	fail "malformed stdin lacks the corruption diagnostic"

if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -d "$malformed_archive" \
	> "$work_directory/named.stdout" 2> "$work_directory/named.stderr"; then
	fail "malformed named archive reported success"
fi
[ -f "$malformed_archive" ] || fail "malformed archive was removed"
[ ! -e "$work_directory/malformed" ] || fail "partial named output remains"
grep -q '^uncompress: corrupt input$' "$work_directory/named.stderr" ||
	fail "malformed named archive lacks the corruption diagnostic"

for header_bits in 8 31; do
	invalid_header=$work_directory/header-$header_bits.Z
	printf '\037\235' > "$invalid_header"
	printf "\\$(printf '%03o' "$header_bits")" >> "$invalid_header"
	if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -dc < "$invalid_header" \
		> "$work_directory/header-$header_bits.stdout" \
		2> "$work_directory/header-$header_bits.stderr"; then
		fail "$header_bits-bit header reported success"
	fi
	grep -q "compressed with $header_bits bits" \
		"$work_directory/header-$header_bits.stderr" ||
		fail "$header_bits-bit header lacks the range diagnostic"
done

truncated_header=$work_directory/header-truncated.Z
printf '\037\235' > "$truncated_header"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -dc < "$truncated_header" \
	> "$work_directory/header-truncated.stdout" \
	2> "$work_directory/header-truncated.stderr"; then
	fail "truncated header reported success"
fi
grep -q '^stdin: truncated compressed header$' \
	"$work_directory/header-truncated.stderr" ||
	fail "truncated header lacks the truncation diagnostic"

bad_magic_archive=$work_directory/bad-magic.Z
printf 'not an LZW stream\n' > "$bad_magic_archive"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -d "$bad_magic_archive" \
	> "$work_directory/bad-magic.stdout" \
	2> "$work_directory/bad-magic.stderr"; then
	fail "bad magic in a named archive reported success"
fi
[ -f "$bad_magic_archive" ] || fail "bad-magic archive was removed"
[ ! -e "$work_directory/bad-magic" ] ||
	fail "bad-magic archive created an output file"
grep -q "^$bad_magic_archive: not in compressed format$" \
	"$work_directory/bad-magic.stderr" ||
	fail "bad-magic archive lacks the format diagnostic"

metadata_failure_source=$work_directory/compress-metadata-failure.c
"$PYTHON" - "$source_directory/compress.c" "$metadata_failure_source" <<'PY'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
mutated_path = Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")
anchor = """if (fchown(named_transaction.staged_descriptor,
        named_transaction.input_status.st_uid,
        named_transaction.input_status.st_gid) < 0 ||"""
if source.count(anchor) != 1:
    raise SystemExit("metadata-failure mutation anchor count differs")
source = source.replace(anchor, "if ((errno = EIO) != 0 ||")
mutated_path.write_text(source, encoding="utf-8")
PY
$compiler $common_flags $sanitizer_flags \
	-o "$work_directory/compress-metadata-failure" \
	"$metadata_failure_source" "$publication_shim" ||
	fail "metadata-failure mutation build"
metadata_input=$work_directory/meta
cp "$input_file" "$metadata_input"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$work_directory/compress-metadata-failure" -f "$metadata_input" \
	> "$work_directory/metadata-failure.stdout" \
	2> "$work_directory/metadata-failure.stderr"; then
	fail "metadata failure reported success"
fi
[ -f "$metadata_input" ] || fail "metadata failure removed the input"
[ ! -e "$metadata_input.Z" ] ||
	fail "metadata failure left the destination"

mutated_source=$work_directory/compress-unbounded.c
"$PYTHON" - "$source_directory/compress.c" "$mutated_source" <<'PY'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
mutated_path = Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")
mutations = {
    """\tif (*stack_pointer >= de_stack_end)
\t\tcorrupt_input();
""": "",
    """\tif (code > free_ent)
\t    corrupt_input();
""": "",
    """\t    if (code >= free_ent)
\t\tcorrupt_input();
""": "",
    """\t    if (prefix_code >= code)
\t\tcorrupt_input();
""": "",
}
for old, new in mutations.items():
    if source.count(old) != 1:
        raise SystemExit("mutation anchor count differs for " + repr(old))
    source = source.replace(old, new)
mutated_path.write_text(source, encoding="utf-8")
PY
$compiler $common_flags $sanitizer_flags -o "$work_directory/compress-unbounded" \
	"$mutated_source" "$publication_shim" ||
	fail "unbounded mutation build"
if ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	"$work_directory/compress-unbounded" -dc < "$malformed_archive" \
	> "$work_directory/mutation.stdout" \
	2> "$work_directory/mutation.stderr"; then
	fail "unbounded mutation reported success"
fi
grep -q 'AddressSanitizer: global-buffer-overflow' \
	"$work_directory/mutation.stderr" || {
	sed -n '1,20p' "$work_directory/mutation.stderr" >&2
	fail "unbounded mutation did not reproduce the htab overflow"
}

publication_input=$work_directory/pub
publication_trace=$work_directory/publication.trace
cp "$input_file" "$publication_input"
COMPRESS_TEST_TRACE=$publication_trace \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -f "$publication_input" \
	> "$work_directory/publication.stdout" \
	2> "$work_directory/publication.stderr" ||
	fail "named publication failed"
grep -qx 'publishat_beneath' "$publication_trace" ||
	fail "named output bypassed descriptor-conditioned publication"
grep -qx 'unlinkat_beneath' "$publication_trace" ||
	fail "named input bypassed descriptor-conditioned removal"

assert_no_stage()
{
	stage_directory=$1
	if find "$stage_directory" -maxdepth 1 -name '.cmp*' -print |
		grep -q .; then
		fail "staged output remains in $stage_directory"
	fi
}

assert_foreign_stage()
{
	stage_directory=$1
	if ! find "$stage_directory" -maxdepth 1 -type f -name '.cmp*' \
		-exec grep -lx 'foreign staged replacement' {} \; |
		grep -q .; then
		fail "foreign staged replacement is absent in $stage_directory"
	fi
	if ! find "$stage_directory" -maxdepth 1 -type f \
		-name '.cmp*.opened' -print | grep -q .; then
		fail "descriptor-owned staged inode is absent in $stage_directory"
	fi
}

interposed_binary=$work_directory/compress-interposed
source_race_directory=$work_directory/source-race
source_race_trace=$source_race_directory/interposition.trace
mkdir "$source_race_directory"
cp "$input_file" "$source_race_directory/input"
printf '%s\n' 'replacement input' > "$source_race_directory/input.replacement"
source_race_status=0
COMPRESS_TEST_TRACE=$source_race_trace COMPRESS_TEST_REPLACE_INPUT=input \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$source_race_directory/input" \
	> "$source_race_directory/stdout" 2> "$source_race_directory/stderr" ||
	source_race_status=$?
grep -qx 'replace_input' "$source_race_trace" ||
	fail "source replacement interposition was bypassed"
if [ "$source_race_status" -eq 0 ]; then
	fail "source replacement reported success"
fi
printf '%s\n' 'replacement input' > "$source_race_directory/expected"
cmp "$source_race_directory/expected" "$source_race_directory/input" ||
	fail "source replacement bytes were removed"
[ -f "$source_race_directory/input.opened" ] ||
	fail "source replacement lost the opened input"
[ -f "$source_race_directory/input.Z" ] ||
	fail "source replacement did not preserve the published output"
assert_no_stage "$source_race_directory"

target_race_directory=$work_directory/target-race
mkdir "$target_race_directory"
cp "$input_file" "$target_race_directory/input"
printf '%s\n' 'old target' > "$target_race_directory/input.Z"
printf '%s\n' 'replacement target' > \
	"$target_race_directory/input.Z.replacement"
if COMPRESS_TEST_REPLACE_TARGET=input.Z \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$target_race_directory/input" \
	> "$target_race_directory/stdout" 2> "$target_race_directory/stderr"; then
	fail "target replacement reported success"
fi
printf '%s\n' 'replacement target' > "$target_race_directory/expected"
cmp "$target_race_directory/expected" "$target_race_directory/input.Z" ||
	fail "target replacement bytes were overwritten"
[ -f "$target_race_directory/input" ] ||
	fail "target replacement removed the input"
[ -f "$target_race_directory/input.Z.opened" ] ||
	fail "target replacement lost the opened target"
assert_no_stage "$target_race_directory"

publication_target_directory=$work_directory/publication-target-race
mkdir "$publication_target_directory"
cp "$input_file" "$publication_target_directory/input"
printf '%s\n' 'old target' > "$publication_target_directory/input.Z"
printf '%s\n' 'publication replacement' > \
	"$publication_target_directory/input.Z.replacement"
if COMPRESS_TEST_REPLACE_TARGET_PUBLICATION=input.Z \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$publication_target_directory/input" \
	> "$publication_target_directory/stdout" \
	2> "$publication_target_directory/stderr"; then
	fail "publication-time target replacement reported success"
fi
printf '%s\n' 'publication replacement' > \
	"$publication_target_directory/expected"
cmp "$publication_target_directory/expected" \
	"$publication_target_directory/input.Z" ||
	fail "publication-time target replacement bytes were overwritten"
[ -f "$publication_target_directory/input.Z.opened" ] ||
	fail "publication-time target replacement lost the authorized target"
[ -f "$publication_target_directory/input" ] ||
	fail "publication-time target replacement removed the input"
assert_no_stage "$publication_target_directory"

for stage_replacement in METADATA PUBLICATION; do
	stage_race_directory=$work_directory/stage-race-$stage_replacement
	mkdir "$stage_race_directory"
	cp "$input_file" "$stage_race_directory/input"
	if env "COMPRESS_TEST_REPLACE_STAGE_$stage_replacement=1" \
		ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$interposed_binary" -f "$stage_race_directory/input" \
		> "$stage_race_directory/stdout" \
		2> "$stage_race_directory/stderr"; then
		fail "$stage_replacement stage replacement reported success"
	fi
	[ -f "$stage_race_directory/input" ] ||
		fail "$stage_replacement stage replacement removed the input"
	[ ! -e "$stage_race_directory/input.Z" ] ||
		fail "$stage_replacement stage replacement published the output"
	assert_foreign_stage "$stage_race_directory"
done

cleanup_race_directory=$work_directory/stage-race-cleanup
mkdir "$cleanup_race_directory"
cp "$input_file" "$cleanup_race_directory/input"
if COMPRESS_TEST_FAIL_FCHMOD=1 COMPRESS_TEST_REPLACE_STAGE_CLEANUP=1 \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$cleanup_race_directory/input" \
	> "$cleanup_race_directory/stdout" \
	2> "$cleanup_race_directory/stderr"; then
	fail "cleanup-time stage replacement reported success"
fi
[ -f "$cleanup_race_directory/input" ] ||
	fail "cleanup-time stage replacement removed the input"
[ ! -e "$cleanup_race_directory/input.Z" ] ||
	fail "cleanup-time stage replacement published the output"
assert_foreign_stage "$cleanup_race_directory"

removal_race_directory=$work_directory/input-removal-race
mkdir "$removal_race_directory"
cp "$input_file" "$removal_race_directory/input"
printf '%s\n' 'input-removal replacement' > \
	"$removal_race_directory/input.replacement"
if COMPRESS_TEST_REPLACE_INPUT_REMOVAL=input \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$removal_race_directory/input" \
	> "$removal_race_directory/stdout" \
	2> "$removal_race_directory/stderr"; then
	fail "input-removal replacement reported success"
fi
printf '%s\n' 'input-removal replacement' > \
	"$removal_race_directory/expected"
cmp "$removal_race_directory/expected" "$removal_race_directory/input" ||
	fail "input-removal replacement bytes were removed"
[ -f "$removal_race_directory/input.opened" ] ||
	fail "input-removal replacement lost the opened input"
[ -f "$removal_race_directory/input.Z" ] ||
	fail "input-removal replacement omitted the published output"
assert_no_stage "$removal_race_directory"

overwrite_directory=$work_directory/overwrite
mkdir "$overwrite_directory"
cp "$input_file" "$overwrite_directory/input"
printf '%s\n' 'keep target' > "$overwrite_directory/input.Z"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" "$overwrite_directory/input" \
	> "$overwrite_directory/stdout" 2> "$overwrite_directory/stderr" ||
	fail "overwrite refusal returned failure"
printf '%s\n' 'keep target' > "$overwrite_directory/expected"
cmp "$overwrite_directory/expected" "$overwrite_directory/input.Z" ||
	fail "overwrite refusal changed the target"
[ -f "$overwrite_directory/input" ] ||
	fail "overwrite refusal removed the input"
assert_no_stage "$overwrite_directory"

write_only_directory=$work_directory/write-only-target
mkdir "$write_only_directory"
cp "$input_file" "$write_only_directory/input"
printf '%s\n' 'replace target' > "$write_only_directory/input.Z"
chmod 200 "$write_only_directory/input.Z"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$write_only_directory/input" \
	> "$write_only_directory/stdout" 2> "$write_only_directory/stderr" ||
	fail "write-only destination rejected historical overwrite access"
chmod 600 "$write_only_directory/input.Z"
[ ! -e "$write_only_directory/input" ] ||
	fail "write-only destination retained the input after publication"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$compress_binary" -dc "$write_only_directory/input.Z" \
	> "$write_only_directory/roundtrip" ||
	fail "write-only destination archive could not be decoded"
cmp "$input_file" "$write_only_directory/roundtrip" ||
	fail "write-only destination round trip differs"
assert_no_stage "$write_only_directory"

if [ "$(id -u)" -eq 0 ]; then
	echo "compresscheck: SKIP search-only parent requires a non-privileged UID"
else
	search_only_directory=$work_directory/search-only-parent
	mkdir "$search_only_directory"
	cp "$input_file" "$search_only_directory/input"
	chmod 0300 "$search_only_directory"
	if ! ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -f "$search_only_directory/input" \
		> "$work_directory/search-only.stdout" \
		2> "$work_directory/search-only.stderr"; then
		chmod 0700 "$search_only_directory"
		fail "write-and-search-only parent rejected named publication"
	fi
	chmod 0700 "$search_only_directory"
	[ ! -e "$search_only_directory/input" ] ||
		fail "search-only parent retained the input after publication"
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$compress_binary" -dc "$search_only_directory/input.Z" \
		> "$search_only_directory/roundtrip" ||
		fail "search-only parent archive could not be decoded"
	cmp "$input_file" "$search_only_directory/roundtrip" ||
		fail "search-only parent round trip differs"
	assert_no_stage "$search_only_directory"
fi

for failure_kind in allocation fchown fchmod futimes write flush close \
	input-close dup \
	publication signal; do
	failure_directory=$work_directory/failure-$failure_kind
	mkdir "$failure_directory"
	cp "$input_file" "$failure_directory/input"
	case $failure_kind in
	allocation) failure_environment='COMPRESS_TEST_FAIL_MALLOC_CALL=2' ;;
	fchown) failure_environment='COMPRESS_TEST_FAIL_FCHOWN=1' ;;
	fchmod) failure_environment='COMPRESS_TEST_FAIL_FCHMOD=1' ;;
	futimes) failure_environment='COMPRESS_TEST_FAIL_FUTIMES=1' ;;
	write) failure_environment='COMPRESS_TEST_FAIL_WRITE=1' ;;
	flush) failure_environment='COMPRESS_TEST_FAIL_FLUSH=1' ;;
	close) failure_environment='COMPRESS_TEST_FAIL_CLOSE=1' ;;
	input-close) failure_environment='COMPRESS_TEST_FAIL_FCLOSE_CALL=2' ;;
	dup) failure_environment='COMPRESS_TEST_FAIL_DUP_CALL=2' ;;
	publication) failure_environment='COMPRESS_TEST_FAIL_PUBLICATION=1' ;;
	signal) failure_environment='COMPRESS_TEST_SIGNAL_WRITE=1' ;;
	esac
	if env "$failure_environment" ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$interposed_binary" -f "$failure_directory/input" \
		> "$failure_directory/stdout" 2> "$failure_directory/stderr"; then
		fail "$failure_kind failure reported success"
	fi
	[ -f "$failure_directory/input" ] ||
		fail "$failure_kind failure removed the input"
	[ ! -e "$failure_directory/input.Z" ] ||
		fail "$failure_kind failure published the output"
	assert_no_stage "$failure_directory"
done

removal_failure_directory=$work_directory/failure-removal
mkdir "$removal_failure_directory"
cp "$input_file" "$removal_failure_directory/input"
if COMPRESS_TEST_FAIL_REMOVAL=1 \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$removal_failure_directory/input" \
	> "$removal_failure_directory/stdout" \
	2> "$removal_failure_directory/stderr"; then
	fail "input-removal failure reported success"
fi
[ -f "$removal_failure_directory/input" ] ||
	fail "input-removal failure removed the input"
[ -f "$removal_failure_directory/input.Z" ] ||
	fail "input-removal failure omitted the complete published output"
assert_no_stage "$removal_failure_directory"

cleanup_failure_directory=$work_directory/failure-cleanup
mkdir "$cleanup_failure_directory"
cp "$input_file" "$cleanup_failure_directory/input"
if COMPRESS_TEST_FAIL_WRITE=1 COMPRESS_TEST_FAIL_CLEANUP=1 \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$cleanup_failure_directory/input" \
	> "$cleanup_failure_directory/stdout" \
	2> "$cleanup_failure_directory/stderr"; then
	fail "staged-cleanup failure reported success"
fi
[ -f "$cleanup_failure_directory/input" ] ||
	fail "staged-cleanup failure removed the input"
[ ! -e "$cleanup_failure_directory/input.Z" ] ||
	fail "staged-cleanup failure published the output"
if ! find "$cleanup_failure_directory" -maxdepth 1 -name '.cmp*' -print |
	grep -q .; then
	fail "staged-cleanup failure omitted the retained staged file"
fi
sed -n '1p' "$cleanup_failure_directory/stderr" |
	grep -Fq 'No space left on device' ||
	fail "write failure did not retain its primary diagnostic"
sed -n '2p' "$cleanup_failure_directory/stderr" |
	grep -Fq 'Input/output error' ||
	fail "staged-cleanup failure omitted its secondary diagnostic"

stage_signal_directory=$work_directory/signal-stage-open
mkdir "$stage_signal_directory"
cp "$input_file" "$stage_signal_directory/input"
if COMPRESS_TEST_SIGNAL_STAGE_OPEN=1 \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$stage_signal_directory/input" \
	> "$stage_signal_directory/stdout" \
	2> "$stage_signal_directory/stderr"; then
	fail "stage-open interrupt reported success"
fi
[ -f "$stage_signal_directory/input" ] ||
	fail "stage-open interrupt removed the input"
assert_no_stage "$stage_signal_directory"

for mask_failure in BLOCK RESTORE; do
	mask_failure_directory=$work_directory/mask-$mask_failure
	mkdir "$mask_failure_directory"
	cp "$input_file" "$mask_failure_directory/input"
	if env "COMPRESS_TEST_FAIL_MASK_$mask_failure=1" \
		ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
		"$interposed_binary" -f "$mask_failure_directory/input" \
		> "$mask_failure_directory/stdout" \
		2> "$mask_failure_directory/stderr"; then
		fail "signal-mask $mask_failure failure reported success"
	fi
	[ -f "$mask_failure_directory/input" ] ||
		fail "signal-mask $mask_failure failure removed the input"
	[ ! -e "$mask_failure_directory/input.Z" ] ||
		fail "signal-mask $mask_failure failure published the output"
	assert_no_stage "$mask_failure_directory"
done

unmasked_source=$work_directory/compress-unmasked-stage.c
"$PYTHON" - "$source_directory/compress.c" "$unmasked_source" <<'PY'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
mutated_path = Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")
anchor = """    if (block_interrupt(&previous_mask) < 0)
        terminate_after_signal_mask_failure();

    for (attempt = 0; attempt < 65536U; attempt++) {
"""
replacement = """    if (sigprocmask(SIG_SETMASK, NULL, &previous_mask) < 0)
        terminate_after_signal_mask_failure();

    for (attempt = 0; attempt < 65536U; attempt++) {
"""
if source.count(anchor) != 1:
    raise SystemExit("stage-signal mutation anchor count differs")
mutated_path.write_text(source.replace(anchor, replacement), encoding="utf-8")
PY
$compiler $common_flags $fortify_flags $sanitizer_flags \
	-include "$interposition_header" -c \
	-o "$work_directory/compress-unmasked-stage.o" "$unmasked_source" ||
	fail "stage-signal mutation build"
$compiler $sanitizer_flags -o "$work_directory/compress-unmasked-stage" \
	"$work_directory/compress-unmasked-stage.o" \
	"$work_directory/publication-identity-shim.o" ||
	fail "stage-signal mutation link"
unmasked_directory=$work_directory/signal-stage-unmasked
mkdir "$unmasked_directory"
cp "$input_file" "$unmasked_directory/input"
if COMPRESS_TEST_SIGNAL_STAGE_OPEN=1 \
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$work_directory/compress-unmasked-stage" -f \
	"$unmasked_directory/input" > "$unmasked_directory/stdout" \
	2> "$unmasked_directory/stderr"; then
	fail "unmasked stage-signal mutation reported success"
fi
if ! find "$unmasked_directory" -maxdepth 1 -name '.cmp*' -print |
	grep -q .; then
	fail "unmasked stage-signal mutation did not leave its staged file"
fi

symlink_directory=$work_directory/symlink
mkdir "$symlink_directory"
cp "$input_file" "$symlink_directory/input"
printf '%s\n' 'symlink target' > "$symlink_directory/target"
ln -s target "$symlink_directory/input.Z"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	"$interposed_binary" -f "$symlink_directory/input" \
	> "$symlink_directory/stdout" 2> "$symlink_directory/stderr"; then
	fail "target symlink substitution reported success"
fi
[ -L "$symlink_directory/input.Z" ] ||
	fail "target symlink substitution replaced the link"
[ -f "$symlink_directory/input" ] ||
	fail "target symlink substitution removed the input"
assert_no_stage "$symlink_directory"

echo "compresscheck: PASS"
