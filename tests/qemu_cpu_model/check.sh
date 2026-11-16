#!/bin/sh
# Usage: check.sh QEMU_ARM CPU
# Runs the control programs built by the Makefile on CPU and on "max" and
# compares each exit status with the one the ARMv6-M contract predicts.
set -eu

qemu=$1
cpu=$2
status=0

# Two controls end on a fatal guest signal, and qemu-user then writes the
# guest's core into the working directory when the limit allows one.
ulimit -c 0

expect()
{
	program=$1
	model=$2
	want=$3
	got=0
	# The group redirect also silences the shell's own signal report.
	{ "$qemu" -cpu "$model" "./$program" >/dev/null 2>&1; } 2>/dev/null || got=$?
	if [ "$got" -eq "$want" ]; then
		echo "qemu cpu model: $program on $model: status $got, as expected"
	else
		echo "qemu cpu model: FAIL: $program on $model: status $got, expected $want" >&2
		status=1
	fi
}

expect control_baseline "$cpu" 0
expect control_baseline max 0
expect control_thumb2 "$cpu" 132
expect control_thumb2 max 0
expect control_unaligned "$cpu" 135
expect control_unaligned max 0

exit "$status"
