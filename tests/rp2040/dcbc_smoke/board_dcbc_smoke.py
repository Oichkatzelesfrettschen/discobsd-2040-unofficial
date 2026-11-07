#!/usr/bin/env python3
"""Board smoke test for /usr/bin/dc and /usr/bin/bc on lib/libbn.

usage: DISCOBSD_BOARD_DCBC=run PYTHON board_dcbc_smoke.py [unix.uf2,flash.uf2]

The script touches the board only when DISCOBSD_BOARD_DCBC is exactly "run";
unset, empty and every other value exit without opening a port or calling
discobsd-flash. With the variable set it flashes the named images with
discobsd-flash when given, reboots the board through BOOTSEL otherwise, logs in
as operator on the USB console, and runs the checks below. The image must carry
the picocalc profile (dc, bc and /usr/share/misc/bc.library).

Checks, each with an exact expected line:
  - dc integer arithmetic and the 2^100 print
  - dc with the extended register option, one register named by -x
  - dc on the 2,001-digit factorial of 808 (the digit count)
  - dc and bc remainder at a fractional scale (1.235 mod 1 at scale 2 is .005)
  - bc scale arithmetic, a recursive function and an array
  - bc function whose only parameter is an array, called with an array argument
  - bc -l: pi to twenty places through the math library
  - the elapsed time of "2 1000 ^ p", reported and not asserted

bc forks a child that runs dc's evaluator, so the bc checks establish that the
fork of the bc image fits the window and the swap. This script was written
without a board run; the tree records no result for it.
"""

import glob
import os
import re
import subprocess
import sys
import time

ESC = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\r")
SENTINEL = re.compile(r"^ZD(\d+)$", re.M)
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
OPT_IN_VARIABLE = "DISCOBSD_BOARD_DCBC"
OPT_IN_VALUE = "run"

# (label, console command, expected output lines)
CHECKS = (
    ("dc add", "echo '3 4 + p' | dc", ["7"]),
    ("dc 2^100", "echo '2 100 ^ p' | dc", ["1267650600228229401496703205376"]),
    ("dc divmod", "echo '_7 3 ~ f' | dc", ["-1", "-2"]),
    ("dc scale", "echo '20 k 1 7 / p' | dc", [".14285714285714285714"]),
    ("dc register", "dc -e '7 sa la p'", ["7"]),
    ("dc -x register", "dc -x -e '9 sa la p'", ["9"]),
    (
        "dc 808! digits",
        "dc -e '[lp li * sp li 1 + si li 809 >f]sf 1sp 1si lfx lp Z p'",
        ["2001"],
    ),
    ("dc scaled remainder", "echo '2 k 1.235 1 % p' | dc", [".005"]),
    ("bc scale", "echo 'scale=5; 1/3' | bc", [".33333"]),
    ("bc scaled remainder", "echo 'scale=2; 1.235%1' | bc", [".005"]),
    (
        "bc function",
        "printf 'define f(n) {\\n if (n<2) return (1)\\n return (n*f(n-1))\\n}\\nf(20)\\n' | bc",
        ["2432902008176640000"],
    ),
    ("bc array", "echo 'a[3]=5; a[3]*2' | bc", ["10"]),
    (
        "bc array argument",
        "printf 'define f(a[]) {\\n return (a[0])\\n}\\nb[0]=7\\nf(b[])\\n' | bc",
        ["7"],
    ),
    ("bc -l pi", "echo 'scale=20; 4*a(1)' | bc -l", ["3.14159265358979323844"]),
    ("bc emit", "echo '1+2' | bc -c", [" 1 2+ps.", "q"]),
)


class Console:
    def __init__(self, serial_module, port):
        self.serial = serial_module
        self.port = serial_module.Serial(port, 115200, timeout=0)

    def reopen(self):
        """Reopen the console after the board re-enumerates, which it can do twice."""
        start = time.time()
        while True:
            time.sleep(2)
            try:
                self.port = self.serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0)
                return
            except (IndexError, self.serial.SerialException):
                if time.time() - start > 60:
                    raise

    def read(self, seconds, stop=None):
        buf = b""
        text = ""
        start = time.time()
        while time.time() - start < seconds:
            try:
                data = self.port.read(4096)
            except self.serial.SerialException:
                self.reopen()
                continue
            if data:
                buf += data
                text = ESC.sub(b"", buf).decode("latin1")
                if stop and stop.search(text):
                    break
            else:
                time.sleep(0.05)
        return ESC.sub(b"", buf).decode("latin1")

    def run(self, command, wait=40):
        """Run COMMAND and return its output lines, ending at the sentinel line.

        The shell echoes the typed line and redraws its prompt, so the text
        between the first newline and the line "ZD<status>" is the output; a
        blank echo first ends an output line that lacks its newline, as the
        last line of "bc -c" does. A command that never prints the sentinel
        returns a TIMEOUT line.
        """
        self.port.write(("%s; echo; echo ZD$?" % command).encode() + b"\r")
        out = self.read(wait, SENTINEL)
        match = SENTINEL.search(out)
        if not match:
            return ["TIMEOUT after %d s" % wait, *out[-300:].split("\n")]
        return [line.rstrip() for line in out[: match.start()].split("\n")[1:] if line.strip()]


def flash(images):
    flasher = "discobsd-flash"
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
    if images:
        subprocess.check_call([flasher, *images.split(",")], **quiet)
    else:
        # Reboot through BOOTSEL: "picotool reboot -f" on a running kernel
        # has left the USB link unable to enumerate.
        subprocess.check_call([flasher, "--bootsel"], **quiet)
        subprocess.check_call([flasher, "--eject"], **quiet)
    time.sleep(4)


def open_console(serial_module):
    start = time.time()
    while time.time() - start < 30 and not glob.glob(PORT_PATTERN):
        time.sleep(0.5)
    time.sleep(1)
    ports = glob.glob(PORT_PATTERN)
    if not ports:
        sys.exit("no DiscoBSD console under /dev/serial/by-id")
    console = Console(serial_module, ports[0])
    console.read(25)
    console.port.write(b"operator\r")
    console.read(4)
    return console


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    import serial

    flash(sys.argv[1] if len(sys.argv) > 1 else "")
    console = open_console(serial)
    bad = 0
    for label, command, expected in CHECKS:
        got = console.run(command)
        ok = got == expected
        bad += not ok
        print("%s: %s %s" % (label, got, "ok" if ok else "WRONG, expected %s" % expected))
    start = time.time()
    got = console.run("echo '2 1000 ^ p' | dc", wait=120)
    print("2 1000 ^ p: %d lines in %.1f s" % (len(got), time.time() - start))
    print("dc and bc smoke: %s" % ("PASS" if bad == 0 else "FAIL (%d wrong)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
