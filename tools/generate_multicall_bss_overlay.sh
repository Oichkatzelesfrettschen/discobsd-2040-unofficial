#!/bin/sh
# Generate a linker-script fragment that gives mutually exclusive applets
# one shared virtual address range for zero-initialized private storage.
set -eu

if [ "$#" -lt 3 ]; then
	echo "usage: $0 output.ld applet.tool.o applet.tool.o ..." >&2
	exit 2
fi

output_path=$1
shift
temporary_path=${output_path}.tmp.$$
trap 'rm -f "$temporary_path"' EXIT HUP INT TERM

{
	printf '%s\n' 'SECTIONS' '{' \
	    '  __app_bss_start = .;' \
	    '  __app_bss_end = __app_bss_start;'
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
		printf '  .app_bss_%s __app_bss_start (NOLOAD) : { %s(.app_bss_%s) } :NONE\n' \
		    "$applet_name" "$object_path" "$applet_name"
		printf '  __app_bss_end = MAX(__app_bss_end, __app_bss_start + SIZEOF(.app_bss_%s));\n' \
		    "$applet_name"
	done
	printf '%s\n' \
	    '  .app_bss_extent __app_bss_start (NOLOAD) : { . = __app_bss_end - __app_bss_start; }' \
	    '}'
	printf 'NOCROSSREFS('
	for object_path do
		object_name=${object_path##*/}
		applet_name=${object_name%.tool.o}
		printf ' .app_bss_%s' "$applet_name"
	done
	printf '%s\n' ' );' 'INSERT BEFORE .bss;'
} >"$temporary_path"

mv "$temporary_path" "$output_path"
trap - EXIT HUP INT TERM
