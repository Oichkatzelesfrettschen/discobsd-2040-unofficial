#!/bin/sh
# Admit one canonical machine tuple to a source-directory build tree.

set -eu

if [ "$#" -ne 4 ]; then
	echo "usage: $0 STAMP MACHINE MACHINE_ARCH MACHINE_CPU" >&2
	exit 2
fi

stamp_path=$1
machine=$2
machine_arch=$3
machine_cpu=$4
expected="MACHINE=$machine MACHINE_ARCH=$machine_arch MACHINE_CPU=$machine_cpu"
stamp_parent=${stamp_path%/*}

if [ "$stamp_parent" = "$stamp_path" ]; then
	echo "$stamp_path: architecture stamp needs a parent directory" >&2
	exit 2
fi

mkdir -p "$stamp_parent"

read_stamp()
{
	if [ -L "$stamp_path" ] || [ ! -f "$stamp_path" ]; then
		echo "$stamp_path: architecture stamp is not a regular file" >&2
		exit 2
	fi
	line_count=$(wc -l < "$stamp_path")
	if [ "$line_count" -ne 1 ]; then
		echo "$stamp_path: architecture stamp must contain exactly one line" >&2
		exit 2
	fi
	IFS= read -r actual < "$stamp_path"
	if [ "$actual" != "$expected" ]; then
		echo "$stamp_path: shared artifacts belong to $actual" >&2
		echo "requested tuple is $expected" >&2
		echo "run bmake ${actual%% MACHINE_ARCH=*} cleanall before switching machines" >&2
		exit 2
	fi
}

if [ -e "$stamp_path" ] || [ -L "$stamp_path" ]; then
	read_stamp
	exit 0
fi

temporary_path=$(mktemp "$stamp_path.tmp.XXXXXX")
trap 'rm -f "$temporary_path"' EXIT HUP INT TERM
printf '%s\n' "$expected" > "$temporary_path"

if ln "$temporary_path" "$stamp_path" 2>/dev/null; then
	exit 0
fi

# Another first build may have published the stamp after the existence check.
# Re-read the winner and accept only an identical tuple.
if [ -e "$stamp_path" ] || [ -L "$stamp_path" ]; then
	read_stamp
	exit 0
fi

echo "$stamp_path: cannot publish architecture stamp" >&2
exit 2
