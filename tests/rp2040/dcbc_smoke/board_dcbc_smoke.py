#!/usr/bin/env python3
"""Board smoke test for /usr/bin/dc and /usr/bin/bc on lib/libbn.

usage: DISCOBSD_BOARD_DCBC=run PYTHON board_dcbc_smoke.py [unix.uf2,flash.uf2]

The script touches the board only when DISCOBSD_BOARD_DCBC is exactly "run";
unset, empty and every other value exit without opening a port or calling
picotool. With the variable set it flashes the named images in one BOOTSEL
visit when given, logs in as operator on the USB console, and runs the checks
below. The image must carry the picocalc profile (dc, bc and
/usr/share/misc/bc.library).

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
        "printf 'define f(a[]) { return a[0]; }\\nb[0]=7\\nf(b[])\\n' | bc",
        ["7"],
    ),
    ("bc -l pi", "echo 'scale=20; 4*a(1)' | bc -l", ["3.14159265358979323844"]),
    ("bc emit", "echo '1+2' | bc -c", [" 1 2+ps.", "q"]),
)


class Console:
    def __init__(self, serial_module, port):
        self.port = serial_module.Serial(port, 115200, timeout=0)

    def read(self, seconds):
        buf = b""
        start = time.time()
        while time.time() - start < seconds:
            data = self.port.read(4096)
            if data:
                buf += data
            else:
                time.sleep(0.05)
        return ESC.sub(b"", buf).decode("latin1")

    def run(self, command, wait=8):
        self.port.write(command.encode() + b"\r")
        out = self.read(wait)
        return [
            line for line in out.split("\n")[1:] if line.strip() and not line.startswith("discobsd")
        ]


def flash(images):
    picotool = ("picotool",)
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
    if images:
        subprocess.call([*picotool, "reboot", "-u", "-f"], **quiet)
        time.sleep(3)
        for image in images.split(","):
            subprocess.check_call([*picotool, "load", image], stdout=subprocess.DEVNULL)
        subprocess.check_call([*picotool, "reboot"], stdout=subprocess.DEVNULL)
    else:
        subprocess.call([*picotool, "reboot", "-f"], **quiet)
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
    got = console.run("echo '2 1000 ^ p' | dc", wait=20)
    print(
        "2 1000 ^ p: %d lines in %.1f s including the settle wait" % (len(got), time.time() - start)
    )
    print("dc and bc smoke: %s" % ("PASS" if bad == 0 else "FAIL (%d wrong)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
