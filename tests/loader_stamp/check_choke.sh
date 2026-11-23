#!/bin/sh
# Source-wiring checks on the flash driver that the host test cannot reach:
#  - the boot ROM's erase and program routines are called only from
#    flash_erase() and flash_program(), after flash_write_admitted();
#  - flash_limit_init() decodes the two stamp words with
#    loader_layout_decode(), which the host test executes, stores the end
#    and the swap extent of that layout, and restores the XIP divider only
#    when the layout names the loader's boot2, and flash_enter_xip()
#    restores it after the loader's boot2;
#  - the divider and sample delay flash_ssi_restore() writes equal the ones
#    GizmoBSD's own second stage programs.
# Usage: check_choke.sh FLASH_C BOOT2_S
set -eu

flash_c=$1
boot2_s=$2
for f in "$flash_c" "$boot2_s"; do
	[ -r "$f" ] || {
		echo "check_choke: cannot read $f" >&2
		exit 2
	}
done

awk '
/^flash_erase\(/ { fn = "erase"; guarded = 0; next }
/^flash_program\(/ { fn = "program"; guarded = 0; next }
/^flash_limit_init\(/ { fn = "init"; next }
/^flash_enter_xip\(/ { fn = "enter"; next }
/^}/ { fn = ""; inmagic = 0 }
fn != "" && /if \(!flash_write_admitted\(offset, len, flash_write_end\)\)/ {
	guarded = 1
}
/flrom\.erase\(/ {
	erase_calls++
	if (fn != "erase" || !guarded)
		bad = bad "flrom.erase outside a guarded flash_erase, line " NR "\n"
}
/flrom\.program\(/ {
	program_calls++
	if (fn != "program" || !guarded)
		bad = bad "flrom.program outside a guarded flash_program, line " NR "\n"
}
/flash_write_end = / { end_stores++ }
/flswap_bytes = / { swap_stores++ }
fn == "init" && /^\tloader_layout_decode\(magic, limit, &ll\);$/ { init_decode = 1 }
fn == "init" && /^\tflash_write_end = ll\.write_end;$/ { init_end = 1 }
fn == "init" && /^\tflswap_bytes = ll\.swap_bytes;$/ { init_swap = 1 }
fn == "init" && /^\tif \(ll\.loader_boot2\) \{$/ { inmagic = 1; next }
fn == "init" && inmagic && /^\t\}$/ { inmagic = 0 }
fn == "init" && inmagic && /^\t\tflash_loader_boot2 = 1;$/ { init_flag = 1 }
fn == "init" && inmagic && /^\t\tflash_ssi_restore\(\);$/ { init_restore = 1 }
fn == "enter" && prev ~ /^\tif \(flash_loader_boot2\)$/ &&
    /^\t\tflash_ssi_restore\(\);$/ { enter_restore = 1 }
{ prev = $0 }
END {
	if (erase_calls != 1)
		bad = bad "flrom.erase called " erase_calls + 0 " times\n"
	if (program_calls != 1)
		bad = bad "flrom.program called " program_calls + 0 " times\n"
	if (end_stores != 1 || !init_end)
		bad = bad "flash_write_end is not set once, from the decoded end, in flash_limit_init\n"
	if (!init_decode)
		bad = bad "flash_limit_init does not decode the layout from the stamp words\n"
	if (swap_stores != 1 || !init_swap)
		bad = bad "flswap_bytes is not set once, from the decoded layout, in flash_limit_init\n"
	if (!init_flag || !init_restore)
		bad = bad "flash_limit_init does not restore the XIP divider under the stamp magic\n"
	if (!enter_restore)
		bad = bad "flash_enter_xip does not restore the XIP divider after the loader boot2\n"
	if (bad != "") {
		printf "%s", bad
		exit 1
	}
}
' "$flash_c" || {
	echo "check_choke: FAIL: $flash_c" >&2
	exit 1
}

c_div=$(sed -n 's/^#define[[:space:]]*SSI_XIP_CLKDIV[[:space:]]*\([0-9][0-9]*\)$/\1/p' "$flash_c")
c_dly=$(sed -n 's/^#define[[:space:]]*SSI_XIP_SAMPLE_DLY[[:space:]]*\([0-9][0-9]*\)$/\1/p' "$flash_c")
s_div=$(sed -n 's/^#define PICO_FLASH_SPI_CLKDIV \([0-9][0-9]*\)$/\1/p' "$boot2_s")
s_dly=$(awk '/movs r2, #SSI_RX_SAMPLE_DLY_OFFSET/ { print d; exit }
    /movs r1, #[0-9]/ { d = $3; sub(/^#/, "", d) }' "$boot2_s")
if [ -z "$c_div" ] || [ -z "$c_dly" ] || [ -z "$s_div" ] || [ -z "$s_dly" ]; then
	echo "check_choke: FAIL: divider or sample delay not found" \
	    "(flash.c $c_div/$c_dly, boot2 $s_div/$s_dly)" >&2
	exit 1
fi
if [ "$c_div" != "$s_div" ] || [ "$c_dly" != "$s_dly" ]; then
	echo "check_choke: FAIL: flash.c restores divider $c_div delay $c_dly," \
	    "boot2 programs divider $s_div delay $s_dly" >&2
	exit 1
fi
echo "check_choke: PASS (boot ROM writers behind the bound, stamp wiring," \
    "XIP divider $c_div and sample delay $c_dly match boot2)"
