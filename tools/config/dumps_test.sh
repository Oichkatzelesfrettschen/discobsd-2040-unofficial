#!/bin/sh
# Exercise the config generator's dump-device rule on the PICO Config:
# "dumps on none" writes dumpdev = NODEV, a configuration without a dumps
# line takes the swap device, an explicit device is kept, and "none" beside
# a generic swap is refused as any dump device is.
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 config source-root" >&2
	exit 2
fi

config_program_directory=$(cd "$(dirname "$1")" && pwd)
config_program=$config_program_directory/$(basename "$1")
source_root=$(cd "$2" && pwd)
compile_root=$source_root/sys/arch/rp2040/compile
# config resolves ../../conf from its working directory, so each case gets
# its own directory beside the real compile directories.
test_prefix=$(mktemp -d "$compile_root/config-dumps.XXXXXX")
trap 'rm -rf "$test_prefix" "$test_prefix".*' EXIT HUP INT TERM

# generate NAME SED-SCRIPT: run config on the PICO Config edited by the
# script, in its own directory, and leave its exit status in $status.
generate() {
	mkdir "$test_prefix.$1"
	sed -e "$2" "$compile_root/PICO/Config" >"$test_prefix.$1/Config"
	if (cd "$test_prefix.$1" && "$config_program" Config) \
	    >"$test_prefix/$1.log" 2>&1; then
		status=0
	else
		status=$?
	fi
}

expect_dumpdev() {
	if ! grep -Fqx "$2" "$test_prefix.$1/swapunix.c"; then
		echo "config $1: swapunix.c lacks: $2" >&2
		cat "$test_prefix.$1/swapunix.c" >&2
		exit 1
	fi
}

generate none ''
[ "$status" -eq 0 ] || { cat "$test_prefix/none.log" >&2; exit 1; }
expect_dumpdev none "$(printf 'dev_t\tdumpdev = NODEV;\t\t/* none */')"

generate default '/dumps on none/d'
[ "$status" -eq 0 ] || { cat "$test_prefix/default.log" >&2; exit 1; }
expect_dumpdev default "$(printf 'dev_t\tdumpdev = makedev(0, 8);\t/* fl1` */')"

generate explicit 's/dumps on none/dumps on fl1/'
[ "$status" -eq 0 ] || { cat "$test_prefix/explicit.log" >&2; exit 1; }
expect_dumpdev explicit "$(printf 'dev_t\tdumpdev = makedev(0, 8);\t/* fl1` */')"

generate generic 's/config[[:space:]]*unix[[:space:]]*root on fl0a/config          unix/;s/swap on fl1/swap on generic/'
if [ "$status" -eq 0 ] ||
    ! grep -Fq "dump device specified with generic swap" \
    "$test_prefix/generic.log"; then
	echo "config accepted dumps on none beside a generic swap" >&2
	exit 1
fi

echo "config dump-device test passed"
