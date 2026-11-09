#!/bin/sh
# Calibrate tools/verify_rp2040_swapram_link.sh against synthetic symbol and
# relocation listings. The verifier reads "$NM -S ELF" and "$OBJDUMP -r OBJ";
# here both tools print the named file, so each case is a text fixture. The
# unmodified SwapRAM and non-SwapRAM fixtures must pass, and each mutation
# must fail with the diagnostic that names its defect: a verdict that also
# accepts the broken layout reports nothing.
#
# usage: verify_rp2040_swapram_link_test.sh
set -eu

here=$(cd "$(dirname "$0")" && pwd)
verifier="$here/verify_rp2040_swapram_link.sh"
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT HUP INT TERM

printf '#!/bin/sh\nexec cat "$2"\n' > "$work/listing"
chmod +x "$work/listing"

# Symbols of a 16-KiB SwapRAM kernel with the pool placed after bss.
cat > "$work/good.syms" <<'EOF'
2003e800 T __kernel_data_end
20030000 T __kernel_data_start
20033dd8 B _ebss
20033a90 00000258 b sr_tab
10012dac 00000028 T swapram_codec_acquire
10012dd4 00000024 T swapram_codec_release
50100740 A swapram_codec_work
100131f4 00000068 T swapram_commit
100132a4 000000c0 T swapram_in
10013078 000000f0 T swapram_out
10013208 000000f4 T swapram_reserve
10013400 00000034 T swapram_cancel
20033dd8 00004000 B swapram_pool_mem
1001325c 0000000a T swapram_present
10013168 0000008c T swapram_put
10013266 0000003e T swapram_uarea_prefix
EOF
cat > "$work/vm_swap.relocs" <<'EOF'
00000010 R_ARM_THM_CALL   swapram_reserve
00000018 R_ARM_THM_CALL   swapram_cancel
00000020 R_ARM_THM_CALL   swapram_put
00000030 R_ARM_THM_CALL   swapram_commit
00000040 R_ARM_THM_CALL   swapram_present
00000050 R_ARM_THM_CALL   swapram_in
EOF
cat > "$work/exec.relocs" <<'EOF'
00000010 R_ARM_THM_CALL   swapram_codec_acquire
00000020 R_ARM_THM_CALL   swapram_codec_release
EOF
cat > "$work/sysctl.relocs" <<'EOF'
00000010 R_ARM_THM_CALL   swapram_present
00000020 R_ARM_THM_CALL   swapram_uarea_prefix
EOF
cat > "$work/plain.syms" <<'EOF'
2003e800 T __kernel_data_end
20030000 T __kernel_data_start
20033a00 B _ebss
EOF
: > "$work/empty.relocs"

failures=0

# run SYMS VM EXEC SYSCTL PARAM: verifier status, stderr kept in $work/err.
run()
{
	NM="$work/listing" OBJDUMP="$work/listing" sh "$verifier" \
	    --elf "$1" --object "$2" --exec-object "$3" --sysctl-object "$4" \
	    --param "$5" > /dev/null 2> "$work/err"
}

expect_pass()
{
	if run "$@"; then
		echo "PASS link verifier accepts ${name}"
	else
		echo "FAIL link verifier rejects ${name}:" >&2
		cat "$work/err" >&2
		failures=$((failures + 1))
	fi
}

# mutate NAME SED-EXPRESSION DIAGNOSTIC: edit the good symbols and require
# rejection carrying DIAGNOSTIC.
mutate()
{
	name=$1
	sed "$2" "$work/good.syms" > "$work/mutant.syms"
	if run "$work/mutant.syms" "$work/vm_swap.relocs" "$work/exec.relocs" \
	    "$work/sysctl.relocs" "-DPICO -DSWAPRAM -DSWAPRAM_KB=16"; then
		echo "FAIL link verifier accepts ${name}" >&2
		failures=$((failures + 1))
	elif grep -F -- "$3" "$work/err" > /dev/null; then
		echo "PASS link verifier rejects ${name}"
	else
		echo "FAIL link verifier rejects ${name} without \"$3\":" >&2
		cat "$work/err" >&2
		failures=$((failures + 1))
	fi
}

name="the SwapRAM kernel"
expect_pass "$work/good.syms" "$work/vm_swap.relocs" "$work/exec.relocs" \
    "$work/sysctl.relocs" "-DPICO -DSWAPRAM -DSWAPRAM_KB=16"
name="the kernel without SwapRAM"
expect_pass "$work/plain.syms" "$work/empty.relocs" "$work/empty.relocs" \
    "$work/empty.relocs" "-DPICO_UART"

mutate "an unaligned pool" \
    's/^20033dd8 00004000 B swapram_pool_mem$/20033ddc 00004000 B swapram_pool_mem/' \
    "starts at unaligned"
mutate "a pool before the end of bss" \
    's/^20033dd8 B _ebss$/20033de0 B _ebss/' \
    "precedes _ebss"
mutate "a pool past kernel RAM" \
    's/^2003e800 T __kernel_data_end$/20037000 T __kernel_data_end/' \
    "is outside kernel RAM"
mutate "a pool below kernel RAM" \
    's/^20030000 T __kernel_data_start$/20034000 T __kernel_data_start/' \
    "is outside kernel RAM"
mutate "a pool of the wrong size" \
    's/^20033dd8 00004000 B swapram_pool_mem$/20033dd8 00008000 B swapram_pool_mem/' \
    "PARAM asks for 16 KB"
mutate "an absent end-of-bss symbol" '/ _ebss$/d' "_ebss address is absent"
mutate "an absent kernel RAM bound" '/ __kernel_data_end$/d' \
    "__kernel_data_end address is absent"
mutate "an unlinked tier entry" '/ swapram_commit$/d' \
    "swapram_commit is not linked"
mutate "a moved codec workspace" \
    's/^50100740 A swapram_codec_work$/50100700 A swapram_codec_work/' \
    "expected absolute 50100740"
mutate "an oversized compact table" \
    's/^20033a90 00000258 b sr_tab$/20033a90 00000270 b sr_tab/' \
    "expected 25 compact 24-byte records"
mutate "a retired epoch symbol" \
    '$a\
10013300 00000010 T swapram_enter_large' \
    "retired variable-window symbol swapram_enter_large"

name="a missing vm_swap.o relocation"
sed '/swapram_put$/d' "$work/vm_swap.relocs" > "$work/mutant.relocs"
if run "$work/good.syms" "$work/mutant.relocs" "$work/exec.relocs" \
    "$work/sysctl.relocs" "-DPICO -DSWAPRAM -DSWAPRAM_KB=16"; then
	echo "FAIL link verifier accepts ${name}" >&2
	failures=$((failures + 1))
elif grep -F "no relocation to swapram_put" "$work/err" > /dev/null; then
	echo "PASS link verifier rejects ${name}"
else
	echo "FAIL link verifier rejects ${name} for another reason" >&2
	failures=$((failures + 1))
fi

name="tier symbols in a kernel without SwapRAM"
if run "$work/good.syms" "$work/empty.relocs" "$work/empty.relocs" \
    "$work/empty.relocs" "-DPICO_UART"; then
	echo "FAIL link verifier accepts ${name}" >&2
	failures=$((failures + 1))
elif grep -F "is linked without -DSWAPRAM" "$work/err" > /dev/null; then
	echo "PASS link verifier rejects ${name}"
else
	echo "FAIL link verifier rejects ${name} for another reason" >&2
	failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
	echo "link verifier calibration: $failures case(s) failed" >&2
	exit 1
fi
echo "link verifier calibration: every case passed"
