#!/usr/bin/env python3
"""Board test for the initial user stack alignment.

usage: DISCOBSD_BOARD_STACK_ALIGN=run PYTHON board_stack_align.py [unix.uf2,flash.uf2]
The script touches the board only when DISCOBSD_BOARD_STACK_ALIGN is exactly
"run"; unset, empty and every other value exit without opening a port or
calling discobsd-flash. With the variable set it flashes the named images with
discobsd-flash when given and otherwise reboots the board through BOOTSEL with
"discobsd-flash --bootsel" and "--eject", then logs in as operator on the USB
console and checks the results.

Runs awk's constant arithmetic under environment strings of eight lengths,
which walk the argument block through every 4-byte phase. Every run must
print 2; a 4-byte aligned initial stack makes printf's va_arg(double)
read one word high for half of them.
"""

import glob
import os
import re
import subprocess
import sys
import time

OPT_IN_VARIABLE = "DISCOBSD_BOARD_STACK_ALIGN"
OPT_IN_VALUE = "run"
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"

# The shell redraws its prompt after each command: "operator@pico:~$ ".
PROMPT = re.compile(r"^\S+@\S*:\S*[$#] ?$")
ESC = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\r")
s = None


def reopen():
    """Reopen the console after the board re-enumerates, which it can do twice."""
    global s
    t0 = time.time()
    while True:
        time.sleep(2)
        try:
            s = serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0)
            return
        except (IndexError, serial.SerialException):
            if time.time() - t0 > 60:
                raise


def rd(t):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            d = s.read(4096)
        except serial.SerialException:
            reopen()
            continue
        if d:
            buf += d
        else:
            time.sleep(0.05)
    return ESC.sub(b"", buf).decode("latin1")


def boot(uf2s):
    global s
    flasher = "discobsd-flash"
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
    if uf2s:
        subprocess.check_call([flasher, *uf2s.split(",")], **quiet)
    else:
        # Reboot through BOOTSEL: "picotool reboot -f" on a running kernel
        # has left the USB link unable to enumerate.
        subprocess.check_call([flasher, "--bootsel"], **quiet)
        subprocess.check_call([flasher, "--eject"], **quiet)
    time.sleep(4)
    t0 = time.time()
    while time.time() - t0 < 30 and not glob.glob(PORT_PATTERN):
        time.sleep(0.5)
    time.sleep(1)
    s = serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0)
    rd(25)
    s.write(b"operator\r")
    rd(4)


def run(c, wait=5):
    s.write(c.encode() + b"\r")
    out = rd(wait)
    return [
        line
        for line in out.split("\n")[1:]
        if line.strip() and not line.startswith("discobsd") and not PROMPT.match(line)
    ]


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    global serial
    import serial

    boot(sys.argv[1] if len(sys.argv) > 1 else "")
    bad = 0
    for n in range(8):
        got = run("echo | X=%s awk 'BEGIN{print 1+1}'" % ("1" * n))
        ok = got == ["2"]
        bad += not ok
        print("env %d bytes: %s %s" % (n + 3, got, "ok" if ok else "WRONG"))
    for n in range(4):
        got = run('echo | X=%s awk \'BEGIN{printf "%%d %%.1f %%s\\n", 3, 2.5, "z"}\'' % ("1" * n))
        ok = got == ["3 2.5 z"]
        bad += not ok
        print("printf env %d: %s %s" % (n + 3, got, "ok" if ok else "WRONG"))
    print("stack alignment: %s" % ("PASS" if bad == 0 else "FAIL (%d wrong)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
