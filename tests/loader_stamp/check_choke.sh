#!/bin/sh
# Require the flash driver's two primitive writers to be the only places
# that call the boot ROM's erase and program routines, and to refuse an
# operation through flash_write_admitted() before that call.
# Usage: check_choke.sh FLASH_C
set -eu

flash_c=$1
[ -r "$flash_c" ] || {
	echo "check_choke: cannot read $flash_c" >&2
	exit 2
}

awk '
/^flash_erase\(/ { fn = "erase"; guarded = 0; next }
/^flash_program\(/ { fn = "program"; guarded = 0; next }
/^}/ { fn = "" }
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
END {
	if (erase_calls != 1)
		bad = bad "flrom.erase called " erase_calls + 0 " times\n"
	if (program_calls != 1)
		bad = bad "flrom.program called " program_calls + 0 " times\n"
	if (bad != "") {
		printf "%s", bad
		exit 1
	}
}
' "$flash_c" || {
	echo "check_choke: FAIL: $flash_c" >&2
	exit 1
}
echo "check_choke: PASS (the boot ROM erase and program sit behind the bound)"
