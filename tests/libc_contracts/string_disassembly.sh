#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 arm-objdump object" >&2
	exit 2
fi

arm_objdump=$1
object=$2
temporary_file=$(mktemp)
trap 'rm -f "$temporary_file"' EXIT HUP INT TERM

"$arm_objdump" -dr "$object" > "$temporary_file"
conditional_branches=$(
	awk '
		/<timingsafe_bcmp>:/ { inside = 1; next }
		inside && /^[[:xdigit:]]+ </ { inside = 0 }
		inside && $3 ~ /^b(eq|ne|cs|cc|hi|ls|ge|lt|gt|le|mi|pl|vs|vc)(\.n)?$/ {
			count++
		}
		END { print count + 0 }
	' "$temporary_file"
)
loads=$(
	awk '
		/<timingsafe_bcmp>:/ { inside = 1; next }
		inside && /^[[:xdigit:]]+ </ { inside = 0 }
		inside && $3 == "ldrb" { count++ }
		END { print count + 0 }
	' "$temporary_file"
)

if [ "$conditional_branches" -ne 1 ] || [ "$loads" -ne 2 ]; then
	echo "timingsafe_bcmp: expected one length branch and two byte loads" >&2
	exit 1
fi
if grep -Eq '<(memcmp|bcmp)>' "$temporary_file"; then
	echo "timingsafe_bcmp: comparison delegates to an early-return routine" >&2
	exit 1
fi

echo "timingsafe_bcmp disassembly contract passed"
