#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 MUTANT" >&2
	exit 2
fi

mutant=$1
output=tty_local_mode_signed-shift.mutant.out

if UBSAN_OPTIONS=halt_on_error=1 "$mutant" >"$output" 2>&1; then
	status=0
else
	status=$?
fi

case $status in
1|134)
	accepted_status=yes
	;;
*)
	accepted_status=no
	;;
esac

if [ "$accepted_status" != yes ] || \
    ! grep -F "left shift of 32768" "$output" >/dev/null; then
	cat "$output" >&2
	echo "FAIL tty local-mode signed-shift mutation rejection" >&2
	exit 1
fi

echo "PASS tty local-mode signed-shift mutation rejected"
