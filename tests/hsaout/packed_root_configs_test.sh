#!/bin/sh
# Calibrate the packed-root gate against tracked and untracked configurations,
# unreadable inputs, and each loader object required by EXEC_HSAOUT.
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 verify_packed_root_configs.sh" >&2
	exit 2
fi

verifier=$1
temporary_root=$(mktemp -d)
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM
manifest_directory=$temporary_root/distrib/rp2040
compile_root=$temporary_root/sys/arch/rp2040/compile
pico_directory=$compile_root/PICO
local_directory=$compile_root/LOCAL
mkdir -p "$manifest_directory" "$pico_directory" "$local_directory"
git -C "$temporary_root" init -q

expect_rejection()
{
	expected_message=$1
	shift
	if "$@" 2>"$temporary_root/error"; then
		echo "packed-root verifier accepted invalid input: $expected_message" >&2
		exit 1
	fi
	grep -Fq "$expected_message" "$temporary_root/error"
}

write_valid_makefile()
{
	printf '%s\n' \
		'PARAM += -DEXEC_HSAOUT' \
		'OBJS = subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o' \
		>"$pico_directory/Makefile"
}

expect_rejection 'usage:' sh "$verifier" "$temporary_root" extra-argument
expect_rejection 'cannot read' sh "$verifier" "$temporary_root"
mkdir "$manifest_directory/mi.rp2040"
expect_rejection 'cannot read' sh "$verifier" "$temporary_root"
rmdir "$manifest_directory/mi.rp2040"

printf 'file /bin/sh bin/sh/sh\n' >"$manifest_directory/mi.rp2040"
sh "$verifier" "$temporary_root"

printf 'pack /bin/sh bin/sh/sh\n' >"$manifest_directory/mi.rp2040"
git -C "$temporary_root" add distrib/rp2040/mi.rp2040
expect_rejection 'no tracked RP2040 kernel configurations found' \
	sh "$verifier" "$temporary_root"

printf 'options EXEC_HSAOUT\n' >"$pico_directory/Config"
write_valid_makefile
printf 'options LOCAL_EXPERIMENT\n' >"$local_directory/Config"
printf 'PARAM += -DLOCAL_EXPERIMENT\n' >"$local_directory/Makefile"
git -C "$temporary_root" add \
	sys/arch/rp2040/compile/PICO/Config \
	sys/arch/rp2040/compile/PICO/Makefile
sh "$verifier" "$temporary_root"

printf 'options CORE_DEFAULT=0\n' >"$pico_directory/Config"
expect_rejection 'PICO lacks options EXEC_HSAOUT' sh "$verifier" "$temporary_root"
printf 'options EXEC_HSAOUT\n' >"$pico_directory/Config"

printf '%s\n' \
	'PARAM += -DOTHER_OPTION' \
	'OBJS = subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o' \
	>"$pico_directory/Makefile"
expect_rejection 'PICO Makefile lacks -DEXEC_HSAOUT' \
	sh "$verifier" "$temporary_root"

for required_object in subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o; do
	printf 'PARAM += -DEXEC_HSAOUT\nOBJS =' >"$pico_directory/Makefile"
	for candidate_object in subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o; do
		if [ "$candidate_object" != "$required_object" ]; then
			printf ' %s' "$candidate_object" >>"$pico_directory/Makefile"
		fi
	done
	printf '\n# retained rule and comment decoys: %s\n%s:\n' \
		"$required_object" "$required_object" >>"$pico_directory/Makefile"
	expect_rejection "PICO Makefile lacks $required_object" \
		sh "$verifier" "$temporary_root"
done

printf '%s\n' \
	'PARAM += -DEXEC_HSAOUT' \
	'OBJS = subr_crc32.o exec_hsaout.o \' \
	'       hsx_stream.o hsx_decoder.o' \
	>"$pico_directory/Makefile"
sh "$verifier" "$temporary_root"

printf '%s\n' \
	'PARAM += -DEXEC_HSAOUT' \
	'OBJS = subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o' \
	'OBJS = subr_crc32.o exec_hsaout.o hsx_stream.o hsx_decoder.o' \
	>"$pico_directory/Makefile"
expect_rejection 'PICO Makefile has an invalid OBJS assignment' \
	sh "$verifier" "$temporary_root"

write_valid_makefile
sh "$verifier" "$temporary_root"

echo "packed-root configuration tests passed"
