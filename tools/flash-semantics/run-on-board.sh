#!/bin/sh
# Load the flash-semantics probe into the attached Pico's SRAM, capture its
# report, and wait for the board to come back.
#
# The board reaches BOOTSEL through picotool, the probe is executed from SRAM
# without being written to flash, its USB CDC port is found by the Pico SDK's
# own device name, and the report is read until its END line. The probe's
# watchdog then reboots the board into the image resident in flash, and the
# script waits for that image's console to enumerate again.
#
# This script reaches the board on purpose and only when invoked by hand:
# no gate calls it. It needs picotool, ${PYTHON} with pyserial, and a
# no_flash flash_semantics.elf built by CMake against the Pico SDK.
#
# Usage: sh tools/flash-semantics/run-on-board.sh <flash_semantics.elf> [out]
# The report goes to stdout, or to the file named by the second argument.

set -eu

PYTHON=${PYTHON:-python3}
ELF=${1:?flash_semantics.elf}
OUT=${2:-/dev/stdout}

# The console the resident kernel presents, so its return can be awaited.
RESIDENT_GLOB='/dev/serial/by-id/*DiscoBSD*'
# The Pico SDK's stdio_usb device name.
PROBE_GLOB='/dev/serial/by-id/usb-Raspberry_Pi_Pico*'

wait_for() {
	glob=$1
	limit=$2
	i=0
	while [ "$i" -lt "$limit" ]; do
		for dev in $glob; do
			[ -e "$dev" ] && { echo "$dev"; return 0; }
		done
		sleep 1
		i=$((i + 1))
	done
	return 1
}

[ -r "$ELF" ] || { echo "run-on-board: no $ELF" >&2; exit 2; }
command -v picotool >/dev/null 2>&1 || { echo "run-on-board: no picotool" >&2; exit 2; }

echo "run-on-board: rebooting the board into BOOTSEL" >&2
picotool reboot -u -f >/dev/null
sleep 2
echo "run-on-board: loading the probe into SRAM and running it" >&2
picotool load -x "$ELF" >/dev/null

PORT=$(wait_for "$PROBE_GLOB" 20) || {
	echo "run-on-board: the probe's USB CDC port never appeared" >&2
	exit 1
}
echo "run-on-board: reading $PORT" >&2

# One report, from its first JEDEC_ID line to END. The probe repeats the
# report, so a partial first copy is skipped.
"$PYTHON" - "$PORT" >"$OUT" <<'EOF'
import sys
import serial

with serial.Serial(sys.argv[1], 115200, timeout=30) as port:
    lines = []
    started = False
    while True:
        raw = port.readline()
        if not raw:
            sys.exit("run-on-board: timed out waiting for the report")
        line = raw.decode("ascii", "replace").strip()
        if line.startswith("JEDEC_ID"):
            lines = []
            started = True
        if started:
            lines.append(line)
        if started and line == "END":
            break
print("\n".join(lines))
EOF

echo "run-on-board: waiting for the resident image's console" >&2
if wait_for "$RESIDENT_GLOB" 40 >/dev/null; then
	echo "run-on-board: resident console is back" >&2
else
	echo "run-on-board: the resident console did not return within 40 s" >&2
	exit 1
fi
