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
compile_root=$source_root/sys/arch/rp2040/compile

if ! grep -Eq '^[[:space:]]*pack[[:space:]]+/' "$manifest_path"; then
	exit 0
fi

configuration_count=0
for config_path in "$compile_root"/*/Config; do
	if [ ! -f "$config_path" ]; then
		continue
	fi
	configuration_count=$((configuration_count + 1))
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
done

if [ "$configuration_count" -eq 0 ]; then
	echo "$0: no tracked RP2040 kernel configurations found" >&2
	exit 1
fi
