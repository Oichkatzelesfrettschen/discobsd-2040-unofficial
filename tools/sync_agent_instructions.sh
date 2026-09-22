#!/bin/sh
set -eu

repository_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_path="$repository_root/AGENTS.md"
destination_directory="$repository_root/.claude"
destination_path="$destination_directory/CLAUDE.md"

if [ ! -d "$destination_directory" ] || [ -L "$destination_directory" ]; then
	printf '%s\n' '.claude must be an actual directory' >&2
	exit 2
fi
if [ -d "$destination_path" ]; then
	printf '%s\n' 'refusing directory destination: .claude/CLAUDE.md' >&2
	exit 2
fi

temporary_path=$(mktemp "$destination_directory/.CLAUDE.md.tmp.XXXXXX")
trap 'rm -f "$temporary_path"' EXIT HUP INT TERM
install -m 0644 "$source_path" "$temporary_path"
mv -f "$temporary_path" "$destination_path"
trap - EXIT HUP INT TERM

cmp "$source_path" "$destination_path"
printf '%s\n' 'synchronized .claude/CLAUDE.md from AGENTS.md'
