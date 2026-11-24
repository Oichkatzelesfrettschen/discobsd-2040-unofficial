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
trap 'rm -rf "$test_directory"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

sed \
	-e 's/board[[:space:]]*"PICO"/board           "CONFIG_MAKEFILE"/' \
	-e 's/config[[:space:]]*unix[[:space:]]*root on fl0a/config          unix/' \
	-e 's/swap on fl1/swap on generic/' \
	-e '/dumps on none/d' \
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

# A Config without a board line is refused with a named error and exit status
# 3 (a null board once reached raise() and faulted).
grep -v '^board[[:space:]]' "$test_directory/Config" >"$test_directory/Config.noboard"
if ! grep -q '^board[[:space:]]' "$test_directory/Config" ||
    grep -q '^board[[:space:]]' "$test_directory/Config.noboard"; then
	echo "board-less Config construction failed" >&2
	exit 1
fi
status=0
(cd "$test_directory" && "$config_program" Config.noboard) \
    >/dev/null 2>"$test_directory/noboard.err" || status=$?
if [ "$status" -ne 3 ] ||
    ! grep -q 'missing "board" specification' "$test_directory/noboard.err"; then
	echo "config accepted or crashed on a Config without board (status $status)" >&2
	exit 1
fi
echo "config missing-board refusal test passed"
