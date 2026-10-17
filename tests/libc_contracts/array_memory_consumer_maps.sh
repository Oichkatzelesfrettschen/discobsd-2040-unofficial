#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
	echo "usage: $0 source-root make-command target-machine" >&2
	exit 2
fi

source_root=$1
make_command=$2
target_machine=$3
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

build_map()
{
	consumer_name=$1
	consumer_directory=$2
	consumer_map=$temporary_directory/$consumer_name.map
	"$make_command" -C "$source_root/$consumer_directory" \
	    MACHINE="$target_machine" clean >/dev/null
	"$make_command" -C "$source_root/$consumer_directory" \
	    MACHINE="$target_machine" LINK_MAP="$consumer_map" all >/dev/null
	if [ ! -s "$consumer_map" ]; then
		echo "$consumer_directory ignored LINK_MAP=$consumer_map" >&2
		return 1
	fi
}

require_member()
{
	consumer_map=$1
	required_member=$2
	if ! grep -Fq "libc.a($required_member)" "$consumer_map"; then
		echo "$consumer_map: libc selected no $required_member" >&2
		return 1
	fi
}

reject_direct_member()
{
	consumer_map=$1
	forbidden_member=$2
	if awk -v member="$forbidden_member" \
	    '$1 == "LOAD" {
		component_count = split($2, components, "/")
		if (components[component_count] == member)
			found = 1
	     }
	     END { exit !found }' "$consumer_map"
	then
		echo "$consumer_map: directly linked $forbidden_member" >&2
		return 1
	fi
}

build_map cut usr.bin/cut
build_map paste usr.bin/paste
build_map expand usr.bin/expand
build_map unexpand usr.bin/unexpand
build_map nl usr.bin/nl
build_map uudecode usr.bin/uudecode
build_map textbox sbin/textbox

require_member "$temporary_directory/cut.map" reallocarray.o
require_member "$temporary_directory/cut.map" memmem.o
require_member "$temporary_directory/cut.map" getdelim.o
require_member "$temporary_directory/cut.map" getline.o
for consumer_name in paste expand unexpand
do
	require_member "$temporary_directory/$consumer_name.map" reallocarray.o
done
for consumer_name in nl uudecode
do
	require_member "$temporary_directory/$consumer_name.map" getdelim.o
	require_member "$temporary_directory/$consumer_name.map" getline.o
done
for required_member in reallocarray.o memmem.o getdelim.o getline.o
do
	require_member "$temporary_directory/textbox.map" "$required_member"
done

for consumer_name in cut paste expand unexpand textbox
do
	reject_direct_member "$temporary_directory/$consumer_name.map" reallocarray.o
done
for consumer_name in cut textbox
do
	reject_direct_member "$temporary_directory/$consumer_name.map" memmem.o
done
for consumer_name in cut nl uudecode textbox
do
	reject_direct_member "$temporary_directory/$consumer_name.map" getline.o
done

damaged_map=$temporary_directory/textbox-missing-memmem.map
awk '!index($0, "libc.a(memmem.o)")' "$temporary_directory/textbox.map" > "$damaged_map"
if require_member "$damaged_map" memmem.o >/dev/null 2>&1; then
	echo "consumer-map negative control accepted a missing memmem.o" >&2
	exit 1
fi

direct_map=$temporary_directory/textbox-direct-memmem.map
{
	cat "$temporary_directory/textbox.map"
	echo "LOAD memmem.o"
} > "$direct_map"
if reject_direct_member "$direct_map" memmem.o >/dev/null 2>&1; then
	echo "consumer-map negative control accepted a direct memmem.o" >&2
	exit 1
fi

echo "array-memory consumer link-map contracts passed"
