#!/bin/sh
# Lint gate: every tracked shell script under shellcheck at error severity
# and every tracked Python file under ruff. A script is one whose name ends
# in .sh or whose first line is a sh shebang (share/man/makewhatis.sed
# carries one and is a sed script); .shellcheckrc declares sh for the
# board scripts that carry no shebang. ruff reads ruff.toml at the top
# of the tree and the host package's own pyproject.toml beneath it.
set -eu

TOPSRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOPSRC"

for tool in shellcheck ruff; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "check-lint: $tool is required" >&2
		exit 2
	}
done

# A case statement inside $( ) trips the bash 3.2 that macOS runs as sh,
# so the test is a function and the substitution only calls it.
is_shell_script() {
	case $1 in
	*.sh) return 0 ;;
	*.sed) return 1 ;;
	esac
	case $(head -c 24 "$1" 2>/dev/null | tr -d '\000' | head -n 1) in
	'#!/bin/sh'*|'#!/usr/bin/env sh'*|'#! /bin/sh'*) return 0 ;;
	esac
	return 1
}
scripts=$(git ls-files | while read -r f; do
	is_shell_script "$f" && echo "$f"
done)
# shellcheck disable=SC2086
shellcheck -S error $scripts
echo "check-lint: shellcheck: $(echo "$scripts" | wc -l | tr -d ' ') scripts"

ruff check .
echo "check-lint: ruff: $(git ls-files '*.py' | wc -l | tr -d ' ') files"
