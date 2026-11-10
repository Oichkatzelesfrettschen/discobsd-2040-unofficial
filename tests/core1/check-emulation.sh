#!/bin/sh
# Emulation gate for the core 1 code of a linked RP2040 kernel.
#
# emulate.py runs the kernel's own machine code on two emulated cores and
# has to pass on the built kernel. The gate is then calibrated: each mutant
# below changes one line of kernel source, the kernel is rebuilt from that
# source, and the emulation has to fail on it. The rebuilds happen in a
# scratch tree of symbolic links whose only real files are the mutated
# source and a copy of the board's build directory, so the checked-out tree
# and its build products stay as they were.
#
# Usage: PYTHON=... sh check-emulation.sh [BOARD]   (default PICO_UART)
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
board=${1:-PICO_UART}
make=${MAKE:-bmake}
builddir=sys/arch/rp2040/compile/$board
srcdir=sys/arch/rp2040/rp2040

if [ ! -f "$top/$builddir/unix.elf" ]; then
	echo "core1-emulation: $builddir/unix.elf is missing; build the kernel first" >&2
	exit 2
fi
"$PYTHON" -I -c 'import unicorn, elftools' || {
	echo "core1-emulation: Unicorn and pyelftools are required under PYTHON" >&2
	exit 2
}

"$PYTHON" -I "$here/emulate.py" "$top/$builddir/unix.elf"

work=$(mktemp -d "${TMPDIR:-/tmp}/core1emu.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
tree=$work/tree

# mirror SRC DST SKIP: link every entry of SRC into DST except SKIP.
mirror()
{
	mkdir -p "$2"
	for _entry in "$1"/* "$1"/.[!.]*; do
		[ -e "$_entry" ] || continue
		_name=${_entry##*/}
		case " $3 " in
		*" $_name "*) continue ;;
		esac
		ln -s "$_entry" "$2/$_name"
	done
}

mirror "$top" "$tree" "sys .git .build-logs"
mirror "$top/sys" "$tree/sys" "arch"
mirror "$top/sys/arch" "$tree/sys/arch" "rp2040"
mirror "$top/sys/arch/rp2040" "$tree/sys/arch/rp2040" "rp2040 compile"
mirror "$top/$srcdir" "$tree/$srcdir" ""
mkdir -p "$tree/sys/arch/rp2040/compile"
cp -RPp "$top/$builddir" "$tree/$builddir"

# rebuild LOG: relink the scratch kernel from the scratch sources.
rebuild()
{
	if ! "$make" -C "$tree/$builddir" MACHINE=rp2040 unix >"$1" 2>&1; then
		echo "core1-emulation: the scratch kernel did not build" >&2
		tail -n 30 "$1" >&2
		exit 1
	fi
}

# The scratch tree itself must change nothing: its kernel has to pass.
rebuild "$work/control.log"
"$PYTHON" -I "$here/emulate.py" "$tree/$builddir/unix.elf" >"$work/control.out" || {
	echo "core1-emulation: the unmutated scratch kernel failed" >&2
	cat "$work/control.out" >&2
	exit 1
}

mutants=0

# mutant NAME FILE SED-EXPRESSION: the rebuilt kernel must fail the emulation.
mutant()
{
	rm -f "$tree/$srcdir/$2"
	sed "$3" "$top/$srcdir/$2" >"$tree/$srcdir/$2"
	if cmp -s "$top/$srcdir/$2" "$tree/$srcdir/$2"; then
		echo "core1-emulation: mutant $1 changed nothing" >&2
		exit 1
	fi
	rebuild "$work/$1.log"
	_rc=0
	"$PYTHON" -I "$here/emulate.py" "$tree/$builddir/unix.elf" \
	    >"$work/$1.out" 2>&1 || _rc=$?
	if [ "$_rc" -ne 1 ]; then
		echo "core1-emulation: mutant $1 gave exit $_rc, expected 1" >&2
		tail -n 5 "$work/$1.out" >&2
		exit 1
	fi
	echo "rejected $1: $(sed -n 's/^FAIL core1-emulation [^:]*: //p' "$work/$1.out")"
	rm -f "$tree/$srcdir/$2"
	ln -s "$top/$srcdir/$2" "$tree/$srcdir/$2"
	mutants=$((mutants + 1))
}

s='^[[:space:]]*'

# Core 1's loop.
mutant core1-calls-flash core1.c \
    "s/${s}site = REG32(WATCHDOG_BASE + WATCHDOG_SCRATCH1);\$/(void)clock_beat(); &/"
mutant core1-never-answers core1.c \
    "s/${s}(void)cring_put(&core1.c1_rep, &r);\$/(void)r;/"
mutant systick-pending-kept core1.c \
    "/${s}REG32(SCB_ICSR) = SCB_ICSR_PENDSTCLR;\$/d"
mutant sleepdeep-kept core1.c \
    "/${s}REG32(SCB_SCR) = 0;\$/d"
mutant probe-not-raised core1.c \
    "/${s}REG32(TIMER_BASE + ALIAS_SET + TIMER_INTF) =\$/{N;d;}"
mutant record-keeps-stale-pc core1.c \
    "s/answered = core1_probe\\[1\\] == core1_probe\\[0\\];/answered = 1;/"
mutant reset-without-wdsel core1.c \
    "/${s}REG32(PSM_BASE + PSM_WDSEL) = PSM_WDSEL_ALL_BUT_OSC;\$/d"

# Core 0's probe handler, launch, park and supervisor.
mutant probe-unanswered core1.c \
    "/str	r1, \\[r3, #4\\]/d"
mutant probe-left-asserted core1.c \
    "/ldr	r3, =0x4005703c/,/bx	lr/{/str	r1, \\[r3\\]/d;}"
mutant launch-unbounded core1.c \
    "/${s}do {\$/{n;N;d;}"
mutant ready-word-not-awaited core1.c \
    "/^core1_launch(void)\$/,/^}\$/{/${s}t0 = REG32(TIMER_BASE + TIMER_TIMERAWL);\$/{n;N;d;};}"
mutant vector-table-short core1.c \
    "s/for (i = 2; i < 16; i++)/for (i = 4; i < 16; i++)/"
mutant park-keeps-nmi-route core1.c \
    "s/${s}REG32(SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK) = 0;\$/(void)0;/"
mutant park-keeps-forced-interrupt core1.c \
    "/${s}REG32(TIMER_BASE + ALIAS_CLR + TIMER_INTF) = C1_PROBE_BIT;\$/d"
mutant park-holds-core1-in-reset core1.c \
    "/${s}REG32(PSM_BASE + ALIAS_CLR + PSM_FRCE_OFF) = PSM_FRCE_OFF_PROC1;\$/d"
mutant poll-outlives-core1 core1.c \
    "s/if (core1_sup.cs_state == C1_RUN || core1_sup.cs_state == C1_WAIT)\$/if (1)/"
mutant nmi-route-kept-in-section machdep.c \
    "s/site != WD_SITE_NONE ? 0 : core1_nmi_mask;/core1_nmi_mask;/"

echo "core1-emulation: $board kernel passed, $mutants mutant kernels rejected"
