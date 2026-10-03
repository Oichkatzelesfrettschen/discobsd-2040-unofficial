#!/bin/sh
#
# Exercise the shipped 12-bit LZW configuration at host width.  The malformed
# stream is the CVE-2006-1168 reproducer with its header constrained to the
# target's BITS value; the decoder must reject its cyclic prefix chain before
# the overlaid htab storage ends.
#
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"

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
common_flags="-DUSERMEM=1024 -std=gnu17 -O1 -g -Wall -Wextra -Werror"
sanitizer_flags="-fno-omit-frame-pointer -fsanitize=address,undefined"

$compiler $common_flags $sanitizer_flags -o "$compress_binary" \
	"$source_directory/compress.c" || fail "host sanitizer build"
$compiler $common_flags -DDEBUG -fsyntax-only \
	"$source_directory/compress.c" || fail "DEBUG syntax build"

input_file=$work_directory/input
iteration=0
while [ "$iteration" -lt 256 ]; do
	printf '%s\n' 'DiscoBSD RP2040 bounded LZW round trip' >> "$input_file"
	iteration=$((iteration + 1))
done

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

for option_value in 0 9 12 13 2147483647; do
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
    "bits-attached.Z": 0x89,
}
for filename, expected_header in expected_headers.items():
    archive = (work_directory / filename).read_bytes()
    if archive[:3] != bytes((0x1F, 0x9D, expected_header)):
        raise SystemExit(f"{filename}: unexpected LZW header")
PY

for option_value in 9junk +9 -9 ' 9' '' 2147483648 999999999999999999999999999; do
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
anchor = "if (chown(destination_name, statbuf.st_uid, statbuf.st_gid)) {"
if source.count(anchor) != 1:
    raise SystemExit("metadata-failure mutation anchor count differs")
source = source.replace(anchor, "if (1) {")
mutated_path.write_text(source, encoding="utf-8")
PY
$compiler $common_flags $sanitizer_flags \
	-o "$work_directory/compress-metadata-failure" \
	"$metadata_failure_source" || fail "metadata-failure mutation build"
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
	"$mutated_source" || fail "unbounded mutation build"
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

echo "compresscheck: PASS"
