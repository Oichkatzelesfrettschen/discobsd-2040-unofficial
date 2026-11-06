#!/bin/sh
# Build a copy of the PSRAM driver sources with one edit, and require the
# gate to fail on it. Usage: mutant.sh CC PYTHON SRCDIR FILE SED-SCRIPT
# SRCDIR is sys/arch/rp2040/dev. The edit must change FILE; a mutant the
# gate accepts, or an edit that changes nothing, fails this script.
set -eu

cc=$1
py=$2
src=$3
file=$4
script=$5
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/psram-mutant.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

mkdir -p "$work/rp2040/dev"
cp "$src"/psram_pio.h "$src"/psram_xfer.c "$src"/psram_xfer.h \
    "$src"/picocalc_reg.h "$work/rp2040/dev/"
sed "$script" "$work/rp2040/dev/$file" > "$work/edited"
if cmp -s "$work/edited" "$work/rp2040/dev/$file"; then
	echo "mutant edit did not change $file: $script" >&2
	exit 2
fi
mv "$work/edited" "$work/rp2040/dev/$file"

"$cc" -std=gnu17 -w -DPSRAM_HOST -fPIC -shared \
    -I"$here" -I"$work" -o "$work/lib.so" "$here/psram_host.c" \
    "$work/rp2040/dev/psram_xfer.c"
if (cd "$here" && "$py" test_psram.py --lib "$work/lib.so" \
    --header "$work/rp2040/dev/psram_pio.h" \
    --xfer-header "$work/rp2040/dev/psram_xfer.h" \
    > "$work/out" 2>&1); then
	echo "mutant survived: $file: $script" >&2
	exit 1
fi
echo "mutant killed: $file: $script ($(grep -c '^FAIL' "$work/out" || true) failing checks)"
