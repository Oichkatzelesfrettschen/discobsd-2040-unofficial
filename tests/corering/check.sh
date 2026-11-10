#!/bin/sh
# Host gate for sys/arch/rp2040/rp2040/corering.c, the single-writer rings
# and SIO FIFO doorbell between the two RP2040 cores.
#
# The source compiles unchanged, as strict C17 with warnings as errors,
# against the FIFO register model in corering_test.c: once under the address
# and undefined-behavior sanitizers for the fixed interleavings, once under
# the thread sanitizer for the two-thread exchange. Each mutant below then
# has to fail one of the two runs; a gate that passed one would be checking
# nothing. STRESS sets the round trips of the threaded run.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../sys/arch/rp2040/rp2040
cc=${CC:-cc}
stress=${STRESS:-400000}
work=$(mktemp -d "${TMPDIR:-/tmp}/corering.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

cflags='-std=c17 -O1 -g -Wall -Wextra -Werror -pthread'
TSAN_OPTIONS='halt_on_error=1 exitcode=66'
export TSAN_OPTIONS

# build DIR: compile the copy of the source in DIR into both test programs.
build()
{
	# shellcheck disable=SC2086
	"$cc" $cflags -fsanitize=address,undefined \
	    -fno-sanitize-recover=undefined -I"$1" \
	    -o "$1/det" "$here/corering_test.c"
	# shellcheck disable=SC2086
	"$cc" $cflags -fsanitize=thread -I"$1" \
	    -o "$1/stress" "$here/corering_test.c"
}

mkdir "$work/shipped"
cp "$src/corering.c" "$src/corering.h" "$work/shipped/"
build "$work/shipped"
"$work/shipped/det" det
"$work/shipped/stress" stress "$stress"

mutants=0

# mutant NAME SED-EXPRESSION: the altered source must build and must fail.
mutant()
{
	_dir=$work/$1
	mkdir "$_dir"
	cp "$src/corering.h" "$_dir/"
	sed "$2" "$src/corering.c" >"$_dir/corering.c"
	if cmp -s "$src/corering.c" "$_dir/corering.c"; then
		echo "corering: mutant $1 changed nothing" >&2
		exit 1
	fi
	build "$_dir"
	if "$_dir/det" det >"$_dir/log" 2>&1 &&
	    "$_dir/stress" stress "$stress" >>"$_dir/log" 2>&1; then
		echo "corering: mutant $1 passed" >&2
		exit 1
	fi
	mutants=$((mutants + 1))
}

s='^[[:space:]]*'

# Producer ordering: entry, index, doorbell.
mutant index-before-entry \
    "s/${s}r->cr_entry\\[w & CRING_MASK\\] = \\*e;\$/index_store(\\&r->cr_windex, w + 1); &/"
mutant doorbell-before-index \
    "/${s}bell_ring();\$/d;s/${s}index_store(&r->cr_windex, w + 1);\$/bell_ring(); &/"
mutant doorbell-ignores-rdy \
    "s/if (CRING_RD(SIO_FIFO_ST) & FIFO_ST_RDY)/if (1)/"
mutant ring-overfills \
    "s/>= CRING_SLOTS)\$/> CRING_SLOTS)/"
mutant entry-index-masked-short \
    "s/r->cr_entry\\[w & CRING_MASK\\]/r->cr_entry[w \\& (CRING_MASK >> 1)]/"

# Consumer ordering: empty the FIFO, snapshot, handle, advance.
mutant snapshot-before-fifo-clear \
    "/${s}bell_clear(r);\$/d;s/${s}return n;\$/bell_clear(r); &/"
mutant advance-before-handler \
    "s/${s}fn(ctx, &r->cr_entry\\[rd & CRING_MASK\\]);\$/index_store(\\&r->cr_rindex, rd + 1); &/"
mutant pop-of-empty-fifo \
    "s/${s}while ((st = /(void)CRING_RD(SIO_FIFO_RD); &/"
mutant sticky-error-kept \
    "/CRING_WR(SIO_FIFO_ST, st & FIFO_ST_ERR);/d"
mutant sticky-error-uncounted \
    "s/r->cr_bellerr++;/(void)r;/"

# Initialization and credits.
mutant init-keeps-read-index \
    "/index_store(&r->cr_rindex, 0);/d"
mutant init-keeps-error-count \
    "/r->cr_bellerr = 0;/d"
mutant chan-starts-with-debt \
    "s/c->cc_inflight = 0;/c->cc_inflight = 1;/"
mutant request-without-credit \
    "s/c->cc_inflight >= CRING_SLOTS/0/"
mutant credit-never-returned \
    "/${s}rc->chan->cc_inflight--;\$/d"
mutant credit-before-reply-handler \
    "/${s}rc->chan->cc_inflight--;\$/d;s/${s}rc->fn(rc->ctx, e);\$/rc->chan->cc_inflight--; &/"

echo "corering: shipped source passed, $mutants mutants rejected"
