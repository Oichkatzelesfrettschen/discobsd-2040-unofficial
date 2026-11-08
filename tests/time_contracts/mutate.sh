#!/bin/sh
# Calibrate check.sh: the unmodified source must pass and each mutant must
# be rejected by check.sh itself under every listed compiler. A mutant that
# fails to build, or whose sed expression changes nothing, is an error.
# Usage: mutate.sh TOPSRC TIME_SOURCE COMPILER...
set -eu

top=$1
source_file=$2
shift 2
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# name|sed expression
mutants='
no-eintr-retry|s/w < 0 \&\& errno != EINTR/w < 0/
shift-status|s/return WIFSIGNALED(status) ? .*;/return status >> 8;/
exec-failure-1|s/? 127 : 126/? 1 : 126/
exec-failure-all-127|s/? 127 : 126/? 127 : 127/
retry-any-error|s/if (w < 0 \&\& errno != EINTR) {/if (w < 0 \&\& errno != EINTR \&\& 0) {/
'

for cc in "$@"; do
	command -v "$cc" >/dev/null 2>&1 ||
		{ echo "time mutants: compiler $cc not found" >&2; exit 1; }
	good="$work/$cc-good"
	sh "$here/build.sh" "$cc" "$top" "$source_file" "$good"
	sh "$here/check.sh" "$good" >/dev/null ||
		{ echo "time mutants: $cc: unmodified source rejected" >&2; exit 1; }
	echo "$mutants" | while IFS='|' read -r name expression; do
		[ -n "$name" ] || continue
		mutant="$work/$cc-$name"
		mkdir -p "$mutant"
		sed -e "$expression" "$source_file" >"$mutant/time.c"
		if cmp -s "$source_file" "$mutant/time.c"; then
			echo "time mutants: $name leaves the source unchanged" >&2
			exit 1
		fi
		sh "$here/build.sh" "$cc" "$top" "$mutant/time.c" "$mutant/out"
		if output=$(sh "$here/check.sh" "$mutant/out" 2>&1); then
			echo "time mutants: $cc: $name survived" >&2
			exit 1
		fi
		case "$output" in
		"time contract: "*) echo "$cc $name: rejected: $output" ;;
		*) echo "time mutants: $cc: $name: unexpected output: $output" >&2
		   exit 1 ;;
		esac
	done
done
