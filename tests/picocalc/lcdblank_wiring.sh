#!/bin/sh
#
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# Check the three orderings of the console blanker that lcdblank_test cannot
# reach, by reading the text of the function that holds each one:
#
#   st7365p_sleep()  sets lcd_power_pending before it sends SLPIN, so a
#                    fault during the command finds the panel pending;
#   st7365p_wake()   clears lcd_power_pending only after the 5 ms gap that
#                    follows SLPOUT;
#   kbd_poll()       calls lcdcons_blank_poll() on the line below
#                    "if (r == STEP_EVENT || phase == PH_WRITE)", a
#                    completed exchange.
#
# usage: lcdblank_wiring.sh ST7365P.C PICOKBD.C
#        lcdblank_wiring.sh -m st7365p|picokbd EXPECT SED ST7365P.C PICOKBD.C
#
# Without -m the shipped files must pass. With -m a copy of the named file is
# rewritten by SED and the checks must fail with EXPECT in their message, so a
# mutant counts as rejected only by the check that names its defect.
set -eu

# body FILE FUNCTION: the definition of FUNCTION, from its line to its closing brace.
body() {
	sed -n "/^$2 *(/,/^}/p" "$1"
}

# first_line FILE FUNCTION TEXT: the 1-based line of TEXT within FUNCTION's body.
first_line() {
	body "$1" "$2" | grep -n -F -- "$3" | head -n 1 | cut -d: -f1
}

# check ST7365P.C PICOKBD.C: the three orderings; a failing one is named on stderr.
check() {
	_st=$1
	_kbd=$2
	_a=$(first_line "$_st" st7365p_sleep 'lcd_power_pending = 1;')
	_b=$(first_line "$_st" st7365p_sleep 'lcd_command(CMD_SLPIN);')
	if [ -z "$_a" ] || [ -z "$_b" ] || [ "$_a" -ge "$_b" ]; then
		echo "lcdblank_wiring: FAIL lcd_power_pending is set before SLPIN" >&2
		return 1
	fi
	_a=$(first_line "$_st" st7365p_wake 'mdelay(5);')
	_b=$(first_line "$_st" st7365p_wake 'lcd_power_pending = 0;')
	if [ -z "$_a" ] || [ -z "$_b" ] || [ "$_a" -ge "$_b" ]; then
		echo "lcdblank_wiring: FAIL lcd_power_pending clears after the SLPOUT gap" >&2
		return 1
	fi
	_g=$(first_line "$_kbd" kbd_poll 'if (r == STEP_EVENT || phase == PH_WRITE)')
	_b=$(first_line "$_kbd" kbd_poll 'lcdcons_blank_poll(')
	if [ -z "$_g" ] || [ -z "$_b" ] || [ "$_b" -ne $((_g + 1)) ]; then
		echo "lcdblank_wiring: FAIL blank poll on completed exchanges only" >&2
		return 1
	fi
	return 0
}

mutant=0
if [ "${1:-}" = -m ]; then
	mutant=1
	which=$2
	expect=$3
	expr=$4
	shift 4
fi
if [ $# -ne 2 ]; then
	echo "usage: lcdblank_wiring.sh [-m st7365p|picokbd EXPECT SED] ST7365P.C PICOKBD.C" >&2
	exit 2
fi
if [ "$mutant" -eq 0 ]; then
	check "$1" "$2" || exit 1
	echo "lcdblank_wiring: ok"
	exit 0
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/lcdblank-wiring.XXXXXX")
trap 'rm -f "$work/st7365p.c" "$work/picokbd.c" "$work/mutated"; rmdir "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cp "$1" "$work/st7365p.c"
cp "$2" "$work/picokbd.c"
case $which in
st7365p) name=st7365p.c ;;
picokbd) name=picokbd.c ;;
*)
	echo "lcdblank_wiring: unknown file $which" >&2
	exit 2
	;;
esac
sed "$expr" "$work/$name" >"$work/mutated"
if cmp -s "$work/$name" "$work/mutated"; then
	echo "lcdblank_wiring: '$expr' changed nothing in $name" >&2
	exit 1
fi
mv "$work/mutated" "$work/$name"
rc=0
msg=$(check "$work/st7365p.c" "$work/picokbd.c" 2>&1) || rc=$?
if [ "$rc" -eq 0 ]; then
	echo "lcdblank_wiring: the checks passed the mutant '$expr' of $name" >&2
	exit 1
fi
case $msg in
*"$expect"*) ;;
*)
	echo "lcdblank_wiring: the mutant '$expr' of $name failed without '$expect': $msg" >&2
	exit 1
	;;
esac
echo "mutant rejected: $name '$expr'"
