#!/bin/sh
# Reject localized applet objects whose zero-initialized storage can escape
# the named overlay section. The final link cannot recover applet ownership
# after COMMON or another allocatable NOBITS section reaches it.
set -eu

if [ "$#" -lt 3 ]; then
	echo "usage: $0 nm readelf applet.tool.o ..." >&2
	exit 2
fi

nm_command=$1
readelf_command=$2
shift 2
temporary_output=$(mktemp)
trap 'rm -f "$temporary_output"' EXIT HUP INT TERM

for object_path do
	object_name=${object_path##*/}
	applet_name=${object_name%.tool.o}
	if [ "$applet_name.tool.o" != "$object_name" ]; then
		echo "$0: $object_path does not end in .tool.o" >&2
		exit 2
	fi
	case $applet_name in
		''|*[!A-Za-z0-9_]*)
			echo "$0: $object_path has an unsafe applet name" >&2
			exit 2
			;;
	esac

	if ! "$nm_command" -S --format=posix "$object_path" >"$temporary_output"; then
		echo "$0: $nm_command failed for $object_path" >&2
		exit 1
	fi
	common_symbols=$(awk '$2 == "C" { print $1 }' "$temporary_output")
	if [ -n "$common_symbols" ]; then
		echo "$0: $object_path contains COMMON storage:" >&2
		printf '%s\n' "$common_symbols" >&2
		exit 1
	fi

	if ! "$readelf_command" -SW "$object_path" >"$temporary_output"; then
		echo "$0: $readelf_command failed for $object_path" >&2
		exit 1
	fi
	unexpected_sections=$(
		awk -v expected=".app_bss_$applet_name" '
			/^[[:space:]]*\[/ {
				line = $0
				sub(/^.*\][[:space:]]*/, "", line)
				split(line, fields, /[[:space:]]+/)
				if (fields[2] == "NOBITS" &&
				    fields[7] ~ /W/ && fields[7] ~ /A/ &&
				    fields[1] != expected)
					print fields[1]
			}' "$temporary_output"
	)
	if [ -n "$unexpected_sections" ]; then
		echo "$0: $object_path contains zero-initialized storage outside .app_bss_$applet_name:" >&2
		printf '%s\n' "$unexpected_sections" >&2
		exit 1
	fi
done
