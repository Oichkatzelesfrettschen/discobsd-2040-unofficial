#!/bin/sh
# A root manifest that installs packed executables requires every tracked
# RP2040 kernel configuration to register the packed loader and its objects.
set -eu

if [ "$#" -gt 1 ]; then
	echo "usage: $0 [source-root]" >&2
	exit 2
fi

source_root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
manifest_path=$source_root/distrib/rp2040/mi.rp2040
tracked_configs=$(mktemp)
trap 'rm -f "$tracked_configs"' EXIT HUP INT TERM

if [ ! -f "$manifest_path" ] || [ ! -r "$manifest_path" ]; then
	echo "$0: cannot read $manifest_path" >&2
	exit 1
fi

manifest_status=0
grep -Eq '^[[:space:]]*pack[[:space:]]+/' "$manifest_path" || manifest_status=$?
if [ "$manifest_status" -eq 1 ]; then
	exit 0
fi
if [ "$manifest_status" -ne 0 ]; then
	echo "$0: cannot inspect $manifest_path" >&2
	exit 1
fi

if ! git -C "$source_root" ls-files -- \
    'sys/arch/rp2040/compile/*/Config' >"$tracked_configs"; then
	echo "$0: cannot enumerate tracked RP2040 kernel configurations" >&2
	exit 1
fi

configuration_count=0
while IFS= read -r relative_config_path; do
	configuration_count=$((configuration_count + 1))
	config_path=$source_root/$relative_config_path
	configuration_directory=${config_path%/Config}
	makefile_path=$configuration_directory/Makefile
	configuration_name=${configuration_directory##*/}

	if ! grep -Eq '^[[:space:]]*options[[:space:]]+EXEC_HSAOUT([[:space:]]|$)' "$config_path"; then
		echo "$0: $configuration_name lacks options EXEC_HSAOUT" >&2
		exit 1
	fi
	if ! grep -Eq '^PARAM \+= -DEXEC_HSAOUT$' "$makefile_path"; then
		echo "$0: $configuration_name Makefile lacks -DEXEC_HSAOUT" >&2
		exit 1
	fi
	for object_name in subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o; do
		if ! grep -Eq "(^|[[:space:]])$object_name([[:space:]]|$)" "$makefile_path"; then
			echo "$0: $configuration_name Makefile lacks $object_name" >&2
			exit 1
		fi
	done
done <"$tracked_configs"

if [ "$configuration_count" -eq 0 ]; then
	echo "$0: no tracked RP2040 kernel configurations found" >&2
	exit 1
fi
