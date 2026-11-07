#!/bin/sh
# Relink shipped RP2040 executables twice from their existing objects: once
# against static libc.a and once against a fixed-address libc image imported
# with --just-symbols. Prints raw, packed and packed-block sizes per form.
#
# usage: relink_against_libcx.sh REPO OUTDIR [union|whole]
# REPO holds a completed RP2040 build (lib/libc.a, lib/crt0.o, box *.tool.o,
# program objects, tools/bin/rp2040/{elf2aout,hsaout}). OUTDIR receives every
# generated input and output. A program that still pulls any libc.a member in
# the shared form fails, because a residual member duplicates libc state.
set -eu

[ $# -ge 2 ] || { echo "usage: $0 REPO OUTDIR [union|whole]" >&2; exit 2; }
R=$1
O=$2
SET=${3:-union}
PYTHON=${PYTHON:-python3}
HERE=$(cd "$(dirname "$0")" && pwd)
T=$R/tools/bin/rp2040
CC=arm-none-eabi-gcc

mkdir -p "$O/members" "$O/out"
LIBGCC=$($CC -mcpu=cortex-m0plus -mthumb -print-libgcc-file-name)

# The libc image: text and rodata at a flash address, data and bss at the
# window base. The three upward references libc makes into the program are
# placed in libc RAM, standing in for the libc-owned variables crt0 would set.
(cd "$O/members" && arm-none-eabi-ar x "$R/lib/libc.a")
if [ "$SET" = whole ]; then
    INPUTS=$(ls "$O"/members/*.o)
else
    INPUTS=$(sed "s|^|$O/members/|" "$HERE/libc-union-members.txt")
fi
# shellcheck disable=SC2086
arm-none-eabi-ld -T "$HERE/libcx.ld" --defsym=environ=0x20001ff0 \
    --defsym=__progname=0x20001ff4 --defsym=_end=0x20001ff8 \
    -o "$O/libcx.elf" $INPUTS "$LIBGCC"
arm-none-eabi-objcopy --strip-symbol=environ --strip-symbol=__progname \
    --strip-symbol=_end "$O/libcx.elf" "$O/libcx.syms.elf"
arm-none-eabi-size -A "$O/libcx.elf" > "$O/libcx.size"
arm-none-eabi-nm -g --defined-only "$O/libcx.syms.elf" | awk '{print $3}' |
    sort -u > "$O/libcx.exports"

# Program base after libc RAM, rounded up to 512 bytes.
END=$(arm-none-eabi-nm "$O/libcx.elf" | awk '$3 == "__libc_ram_end" {print $1}')
BASE=$(printf '0x%x' $(( (0x$END + 511) / 512 * 512 )))
sed "s/0x20000000/$BASE/g" "$R/lib/elf32-arm.ld" > "$O/elf32-arm-x.ld"
echo "# libc RAM ends 0x$END; programs link at $BASE"

FL="-Werror -fno-common -mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft -Os -n -nostartfiles -Wl,--fatal-warnings -L$R/lib"

one() { # name dir objs pre post
    p=$1 d=$2 objs=$3 pre=$4 post=$5
    # A program global that libc also exports binds to the libc address under
    # --just-symbols without a diagnostic, so any intersection fails the row.
    clash=$( (cd "$d" && arm-none-eabi-nm -g --defined-only $objs 2> /dev/null) |
        awk 'NF == 3 {print $3}' | sort -u | comm -12 - "$O/libcx.exports")
    if [ -n "$clash" ]; then
        echo "$p FAIL export collision: $(echo $clash)"
        return 0
    fi
    if ! ( cd "$d"
        # shellcheck disable=SC2086
        $CC $FL -T"$R/lib/elf32-arm.ld" $pre "$R/lib/crt0.o" \
            -o "$O/out/$p.base.elf" $objs $post -lc
        # shellcheck disable=SC2086
        $CC $FL -T"$O/elf32-arm-x.ld" $pre \
            -Wl,--just-symbols="$O/libcx.syms.elf" "$R/lib/crt0.o" \
            -o "$O/out/$p.x.elf" $objs $post -lc -Wl,-Map,"$O/out/$p.x.map"
        ) > "$O/out/$p.log" 2>&1; then
        echo "$p FAIL $(grep -m1 -E 'multiple|undefined|error' "$O/out/$p.log")"
        return 0
    fi
    left=$("$PYTHON" "$HERE/map_libc_bytes.py" "$O/out/$p.x.map" "$p" /dev/null |
        sed -n 's/.*libc_img=\([0-9]*\).*libc_bss=\([0-9]*\).*/\1+\2/p')
    if [ "$left" != "0+0" ]; then
        echo "$p FAIL residual libc.a bytes img+bss=$left"
        return 0
    fi
    for v in base x; do
        "$T/elf2aout" "$O/out/$p.$v.elf" "$O/out/$p.$v" > /dev/null
    done
    echo "$p $("$T/hsaout" -s "$O/out/$p.base" | awk 'END {print $2, $3, $5}') $("$T/hsaout" -s "$O/out/$p.x" | awk 'END {print $2, $3, $5}')"
}

echo "# name base_raw base_packed base_blocks x_raw x_packed x_blocks"
for b in box utilbox sysbox textbox adminbox grepbox; do
    d=$R/sbin/$b
    # The overlay script names each *.tool.o, so the objects keep bare names.
    # shellcheck disable=SC2035
    one "$b" "$d" "$(cd "$d" && echo "$b.o" *.tool.o)" "-Wl,-T,$d/multicall-bss-overlay.ld" ""
done
for p in sh ed ps tar fsck init awk sed find login passwd su stevie menu \
    tail tee du as ld compress cpio update getty smlrc; do
    d=
    for b in bin sbin usr.bin usr.sbin libexec; do
        [ -d "$R/$b/$p" ] && { d=$R/$b/$p; break; }
    done
    [ -n "$d" ] || { echo "$p FAIL no source directory"; continue; }
    objs=$(cd "$d" && ls ./*.o | tr '\n' ' ')
    [ "$p" = find ] && objs=find.o
    libs=$(cd "$d" && bmake -V LIBS MACHINE=rp2040 2> /dev/null || true)
    one "$p" "$d" "$objs" "" "$libs"
done
