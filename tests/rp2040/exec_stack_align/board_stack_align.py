#!/usr/bin/env python3
"""Board test for the initial user stack alignment.

usage: PYTHON board_stack_align.py [unix.uf2,flash.uf2]
Flashes the named images in one BOOTSEL visit when given, then logs in
as operator on the USB console and checks the results.

Runs awk's constant arithmetic under environment strings of eight lengths,
which walk the argument block through every 4-byte phase. Every run must
print 2; a 4-byte aligned initial stack makes printf's va_arg(double)
read one word high for half of them.
"""
import glob
import re
import subprocess
import sys
import time

import serial

ESC = re.compile(rb'\x1b\[[0-9;?]*[A-Za-z]|\r')
s = None

def rd(t):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < t:
        d = s.read(4096)
        if d:
            buf += d
        else:
            time.sleep(0.05)
    return ESC.sub(b"", buf).decode("latin1")

def boot(uf2s):
    global s
    if uf2s:
        subprocess.call(
            ["picotool", "reboot", "-u", "-f"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(3)
        for u in uf2s.split(","):
            subprocess.check_call(["picotool", "load", u], stdout=subprocess.DEVNULL)
        subprocess.check_call(["picotool", "reboot"], stdout=subprocess.DEVNULL)
    else:
        subprocess.call(
            ["picotool", "reboot", "-f"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    time.sleep(4)
    t0 = time.time()
    while time.time() - t0 < 30 and not glob.glob("/dev/serial/by-id/*DiscoBSD*rp2040-if00"):
        time.sleep(0.5)
    time.sleep(1)
    s = serial.Serial(glob.glob("/dev/serial/by-id/*DiscoBSD*rp2040-if00")[0], 115200, timeout=0)
    rd(25)
    s.write(b"operator\r")
    rd(4)

def run(c, wait=5):
    s.write(c.encode() + b"\r")
    out = rd(wait)
    return [
        line for line in out.split("\n")[1:]
        if line.strip() and not line.startswith("discobsd")
    ]

uf2s = sys.argv[1] if len(sys.argv) > 1 else ""
boot(uf2s)
bad = 0
for n in range(8):
    got = run("echo | X=%s awk 'BEGIN{print 1+1}'" % ("1" * n))
    ok = got == ["2"]
    bad += not ok
    print("env %d bytes: %s %s" % (n + 3, got, "ok" if ok else "WRONG"))
for n in range(4):
    got = run("echo | X=%s awk 'BEGIN{printf \"%%d %%.1f %%s\\n\", 3, 2.5, \"z\"}'" % ("1" * n))
    ok = got == ["3 2.5 z"]
    bad += not ok
    print("printf env %d: %s %s" % (n + 3, got, "ok" if ok else "WRONG"))
print("stack alignment: %s" % ("PASS" if bad == 0 else "FAIL (%d wrong)" % bad))
sys.exit(1 if bad else 0)
