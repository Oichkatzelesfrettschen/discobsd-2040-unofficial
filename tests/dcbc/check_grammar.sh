#!/bin/sh
# Hold usr.bin/bc/bc.y at the OpenBSD grammar's one shift/reduce and sixteen
# reduce/reduce conflicts.
#
#   check_grammar.sh BISON GRAMMAR
#
# usr.bin/bc builds with bison's conflict warnings off, because an LALR bison
# cannot %expect reduce/reduce conflicts. This script reruns bison with them
# on and matches each whole count: the number is delimited by a space on its
# left and by the conflict noun on its right, so 116 or 216 reduce/reduce
# conflicts do not satisfy sixteen. Before judging GRAMMAR it feeds the same
# verdict a set of known-bad diagnostics (counts that contain 1 or 16 as a
# suffix, a changed count, a missing line) and a copy of GRAMMAR without the
# multiplicative precedence, and fails if any of them passes.
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: check_grammar.sh BISON GRAMMAR" >&2
	exit 2
fi
BISON=$1
GRAMMAR=$2

work=$(mktemp -d "${TMPDIR:-/tmp}/dcbc-grammar.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# diagnostics GRAMMAR: bison's conflict warnings for GRAMMAR.
diagnostics()
{
	"$BISON" -y -Wno-yacc -Wconflicts-sr -Wconflicts-rr -o /dev/null "$1" 2>&1
}

# verdict TEXT: succeeds when TEXT reports exactly 1 shift/reduce and 16
# reduce/reduce conflicts.
verdict()
{
	case "$1" in
	*" 1 shift/reduce conflict "*" 16 reduce/reduce conflicts "*) return 0 ;;
	*) return 1 ;;
	esac
}

# Known-good control: the diagnostics bison prints for the shipped grammar.
good='bc.y: warning: 1 shift/reduce conflict [-Wconflicts-sr]
bc.y: warning: 16 reduce/reduce conflicts [-Wconflicts-rr]'
verdict "$good" || { echo "FAIL dcbc grammar: control rejected the known-good text" >&2; exit 1; }

# Known-bad controls.
while IFS='|' read -r label text; do
	text=$(printf '%s' "$text" | sed 's/\\n/\
/g')
	if verdict "$text"; then
		echo "FAIL dcbc grammar: control accepted $label" >&2
		exit 1
	fi
done <<'CONTROLS'
116 reduce/reduce|bc.y: warning: 1 shift/reduce conflict [-Wconflicts-sr]\nbc.y: warning: 116 reduce/reduce conflicts [-Wconflicts-rr]
216 reduce/reduce|bc.y: warning: 1 shift/reduce conflict [-Wconflicts-sr]\nbc.y: warning: 216 reduce/reduce conflicts [-Wconflicts-rr]
17 reduce/reduce|bc.y: warning: 1 shift/reduce conflict [-Wconflicts-sr]\nbc.y: warning: 17 reduce/reduce conflicts [-Wconflicts-rr]
11 shift/reduce|bc.y: warning: 11 shift/reduce conflict [-Wconflicts-sr]\nbc.y: warning: 16 reduce/reduce conflicts [-Wconflicts-rr]
2 shift/reduce|bc.y: warning: 2 shift/reduce conflicts [-Wconflicts-sr]\nbc.y: warning: 16 reduce/reduce conflicts [-Wconflicts-rr]
no reduce/reduce line|bc.y: warning: 1 shift/reduce conflict [-Wconflicts-sr]
no conflict text|
CONTROLS

# Known-bad grammar: the multiplicative operators lose their precedence.
sed 's/^%left MULTIPLY DIVIDE REMAINDER/%token MULTIPLY DIVIDE REMAINDER/' \
	"$GRAMMAR" > "$work/mutant.y"
if cmp -s "$GRAMMAR" "$work/mutant.y"; then
	echo "FAIL dcbc grammar: the mutant edit did not apply to $GRAMMAR" >&2
	exit 1
fi
if verdict "$(diagnostics "$work/mutant.y")"; then
	echo "FAIL dcbc grammar: control accepted the grammar without multiplicative precedence" >&2
	exit 1
fi

out=$(diagnostics "$GRAMMAR")
if verdict "$out"; then
	echo "PASS dcbc grammar: 1 shift/reduce, 16 reduce/reduce"
else
	echo "FAIL dcbc grammar: $out"
	exit 1
fi
