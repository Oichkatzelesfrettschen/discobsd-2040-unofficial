#!/bin/sh
# Rebuild the cycle-model workload from tree sources and run the model.
#
# usage: PYTHON=python3 build.sh OUTDIR
#
# Steps, all deterministic for a fixed toolchain:
#   1. sample.c -> sample.elf -> sample.bin  (arm-none-eabi-gcc, newlib,
#      -mcpu=cortex-m0plus -mthumb -Os, objcopy -O binary)
#   2. encode.c + the vendored heatshrink encoder, window 9 and lookahead 3,
#      pack sample.bin into sample.hs
#   3. harness.c + hsx_stream.c + hsx_decoder.c + subr_crc32.c, built with the
#      kernel flags and linked at 0x20000000 by harness.ld, give harness.elf
#   4. cycles.py runs run() and run_discard() under Unicorn with the
#      Cortex-M0+ cost table and prints cycles per function
# The script then compares the input hashes with SHA256SUMS and the model
# output with expected-output.txt; any difference is a nonzero exit. Needs
# arm-none-eabi-gcc, a host cc, Unicorn and Capstone for $PYTHON.
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
OUT=${1:?usage: build.sh OUTDIR}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
CC_ARM=${CC_ARM:-arm-none-eabi-gcc}
OBJCOPY_ARM=${OBJCOPY_ARM:-arm-none-eabi-objcopy}
CC_HOST=${CC_HOST:-cc}
HS=$ROOT/sys/arch/rp2040/heatshrink

mkdir -p "$OUT/inc"
ln -sfn "$ROOT/sys/arch/rp2040/include" "$OUT/inc/machine"

# 1. Sample program. Link warnings about unimplemented newlib stubs go to a
# log; they do not affect the image.
"$CC_ARM" -mcpu=cortex-m0plus -mthumb -Os --specs=nosys.specs \
    "$HERE/sample.c" -o "$OUT/sample.elf" 2>"$OUT/sample-link.log"
"$OBJCOPY_ARM" -O binary "$OUT/sample.elf" "$OUT/sample.bin"

# 2. Packed stream.
"$CC_HOST" -O2 -DHEATSHRINK_STATIC_WINDOW_BITS=9 \
    -DHEATSHRINK_STATIC_LOOKAHEAD_BITS=3 -I"$HS" \
    "$HERE/encode.c" "$HS/heatshrink_encoder.c" -o "$OUT/encode"
"$OUT/encode" "$OUT/sample.bin" "$OUT/sample.hs"

# 3. Harness, with the kernel's compile flags.
KFLAGS="-DKERNEL -mcpu=cortex-m0plus -mthumb -ffreestanding -fno-builtin \
-fno-common -std=gnu17 -O -nostdinc"
KINC="-I$ROOT/sys -I$ROOT/sys/arch -I$OUT/inc -I$ROOT/sys/arch/rp2040/dhara/compat \
-I$HS -I$HS/compat"
for src in "$HERE/harness.c" "$ROOT/sys/arch/rp2040/rp2040/hsx_stream.c" \
    "$ROOT/sys/arch/rp2040/rp2040/hsx_decoder.c" "$ROOT/sys/kern/subr_crc32.c"; do
    # shellcheck disable=SC2086
    "$CC_ARM" $KFLAGS $KINC -c "$src" -o "$OUT/$(basename "$src" .c).o"
done
"$CC_ARM" -mcpu=cortex-m0plus -mthumb -nostdlib -nostartfiles \
    -Wl,--no-warn-rwx-segments -Wl,-T,"$HERE/harness.ld" -o "$OUT/harness.elf" \
    "$OUT/harness.o" "$OUT/hsx_stream.o" "$OUT/hsx_decoder.o" "$OUT/subr_crc32.o"

# 4. Model run.
"$PYTHON" -I "$HERE/cycles.py" "$OUT/harness.elf" "$OUT/sample.hs" \
    "$OUT/sample.bin" >"$OUT/run-output.txt"

status=0
(cd "$OUT" && sha256sum -c "$HERE/SHA256SUMS") || status=1
diff "$HERE/expected-output.txt" "$OUT/run-output.txt" || status=1
if [ "$status" -eq 0 ]; then
    echo "build.sh: inputs and model output match the recorded values"
else
    echo "build.sh: mismatch against SHA256SUMS or expected-output.txt" >&2
fi
exit "$status"
