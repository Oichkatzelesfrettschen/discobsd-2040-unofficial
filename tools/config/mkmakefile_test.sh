#!/bin/sh
# Exercise the config generator's physical CFILES line-width contract with a
# generic swap path whose architecture component forces a continuation line.
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 config source-root" >&2
	exit 2
fi

config_program_directory=$(cd "$(dirname "$1")" && pwd)
config_program=$config_program_directory/$(basename "$1")
source_root=$(cd "$2" && pwd)
compile_root=$source_root/sys/arch/rp2040/compile
test_directory=$(mktemp -d "$compile_root/config-makefile.XXXXXX")
trap 'rm -rf "$test_directory"' EXIT HUP INT TERM

sed \
	-e 's/board[[:space:]]*"PICO"/board           "CONFIG_MAKEFILE"/' \
	-e 's/config[[:space:]]*unix[[:space:]]*root on fl0a/config          unix/' \
	-e 's/swap on fl1/swap on generic/' \
	"$compile_root/PICO/Config" >"$test_directory/Config"
printf 'aaaaaaaaaaa.c standard\n' >"$test_directory/files.CONFIG_MAKEFILE"

(
	cd "$test_directory"
	"$config_program" Config
)

if ! grep -Eq '^[[:space:]]+\$A/rp2040/swapgeneric\.c$' \
    "$test_directory/Makefile"; then
	echo "config generator failed to wrap the generic swap C source" >&2
	exit 1
fi

awk '
/^CFILES =/ { in_cfiles = 1 }
in_cfiles && length($0) > 72 {
	printf "CFILES line exceeds 72 columns: %s\n", $0 > "/dev/stderr"
	exit 1
}
in_cfiles && $0 !~ /\\$/ { in_cfiles = 0 }
' "$test_directory/Makefile"

echo "config Makefile CFILES line-width test passed"
