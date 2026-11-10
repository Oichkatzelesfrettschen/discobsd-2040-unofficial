#!/bin/sh
# Host gate for sys/arch/rp2040/rp2040/core1.h: core 1's reading of core 0's
# tick record and core 0's supervision of core 1.
#
# The header compiles unchanged, as strict C17 with warnings as errors,
# under the address and undefined-behavior sanitizers. Each mutant below
# then has to fail the same test; a gate that passed one would be checking
# nothing.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../sys/arch/rp2040/rp2040
cc=${CC:-cc}
work=$(mktemp -d "${TMPDIR:-/tmp}/core1.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

cflags='-std=c17 -O1 -g -Wall -Wextra -Werror'

# build DIR: compile the test against the copy of the header in DIR.
build()
{
	# shellcheck disable=SC2086
	"$cc" $cflags -fsanitize=address,undefined \
	    -fno-sanitize-recover=undefined -I"$1" \
	    -o "$1/core1_test" "$here/core1_test.c"
}

mkdir "$work/shipped"
cp "$src/core1.h" "$work/shipped/"
build "$work/shipped"
"$work/shipped/core1_test"

mutants=0

# mutant NAME SED-EXPRESSION: the altered header must build and must fail.
mutant()
{
	_dir=$work/$1
	mkdir "$_dir"
	sed "$2" "$src/core1.h" >"$_dir/core1.h"
	if cmp -s "$src/core1.h" "$_dir/core1.h"; then
		echo "core1: mutant $1 changed nothing" >&2
		exit 1
	fi
	build "$_dir"
	if "$_dir/core1_test" >"$_dir/log" 2>&1; then
		echo "core1: mutant $1 passed" >&2
		exit 1
	fi
	mutants=$((mutants + 1))
}

s='^[[:space:]]*'

# Core 1's reading of the tick record.
mutant stall-on-one-sample \
    "s/site == C1_SITE_NONE && o->co_site == C1_SITE_NONE;/site == C1_SITE_NONE;/"
mutant stall-on-moving-record \
    "s/int moved = stamp_us != o->co_stamp;/int moved = 0;/"
mutant probe-not-rearmed \
    "/${s}if (moved) {\$/,/${s}}\$/s/o->co_probed = 0;/(void)o;/"
mutant probe-every-sample \
    "s/${s}if (o->co_probed != 0)\$/if (0)/"
mutant stall-threshold-exclusive \
    "s/gap < C1_STALL_US/gap <= C1_STALL_US/"
mutant stall-uncounted \
    "/${s}o->co_stalls++;\$/d"
mutant longest-stall-overwritten \
    "s/if (gap > o->co_gap_us)/if (1)/"
mutant declared-section-counted \
    "s/if (!quiet || gap < C1_STALL_US)/if (!(quiet || 1) || gap < C1_STALL_US)/"
mutant hang-resets-without-second-probe \
    "s/if (o->co_probed == 2)/if (o->co_probed != 0)/"
mutant hang-never-resets \
    "s/${s}o->co_probed = 2;\$/o->co_probed = 1;/"
mutant hang-ignored-in-declared-section \
    "s/if (gap >= C1_HANG_US) {/if (quiet \\&\\& gap >= C1_HANG_US) {/"
mutant record-over-declared-section \
    "s/(site == C1_SITE_NONE ? C1_ACT_RECORD : 0)/C1_ACT_RECORD/"
mutant gap-not-modular \
    "s/unsigned int gap = now_us - stamp_us;/unsigned int gap = now_us > stamp_us ? now_us - stamp_us : 0;/"

# Core 0's supervision.
mutant restart-one-period-late \
    "s/++s->cs_missed >= C1_MISS_MAX/++s->cs_missed > C1_MISS_MAX/"
mutant miss-count-kept \
    "/${s}if (s->cs_pong == s->cs_seq)\$/{n;s/s->cs_missed = 0;/(void)s;/;}"
mutant backoff-constant \
    "s/s->cs_wait = 1U << s->cs_restarts++;/s->cs_wait = 1U; s->cs_restarts++;/"
mutant restarts-unbounded \
    "s/if (s->cs_restarts >= C1_RESTART_MAX) {/if (0) {/"
mutant start-keeps-restart-count \
    "/${s}s->cs_wait = 1;\$/{n;s/s->cs_restarts = 0;/(void)s;/;}"
mutant epoch-not-advanced \
    "/${s}s->cs_epoch++;\$/d"
mutant reply-epoch-unchecked \
    "s/op == C1_OP_PONG && arg == s->cs_epoch &&/op == C1_OP_PONG \\&\\& (arg + 1U != 0U) \\&\\&/"
mutant reply-number-unchecked \
    "s/${s}slot == (s->cs_seq & 0xffffU))\$/slot + 1U != 0U)/"
mutant reply-operation-unchecked \
    "s/if (op == C1_OP_PONG && arg == s->cs_epoch/if (op + 1U != 0U \\&\\& arg == s->cs_epoch/"
mutant failed-launch-runs \
    "/${s}if (!ok)\$/{n;s/return core1_sup_fail(s);/(void)0;/;}"
mutant ping-number-not-advanced \
    "/${s}s->cs_seq++;\$/d"

echo "core1: shipped header passed, $mutants mutants rejected"
