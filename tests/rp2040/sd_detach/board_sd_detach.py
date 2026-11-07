#!/usr/bin/env python3
"""Board check: unmount a read-write SD filesystem after the card was pulled.

usage: DISCOBSD_BOARD_SD_DETACH=run PYTHON board_sd_detach.py

The script touches the board only when DISCOBSD_BOARD_SD_DETACH is exactly
"run"; unset, empty and every other value exit without opening a port. It
flashes nothing: the PicoCalc must already run a kernel with DIOCMEDIAGONE
(sys/sys/disk.h) and an image whose card carries a UFS partition 1 (sd0a)
and whose /mnt exists. With the variable set it logs in as operator on the
USB console and asks the person at the board to pull and reinsert the card.

Steps, each with an exact expected status line:
  - mount /dev/sd0a read-write on /mnt and write a file, so the superblock
    and an inode are dirty
  - the operator removes the card; the card-detect debounce takes 50 ms
  - umount /mnt must succeed with no write-back (before the fix it failed
    with EIO on every attempt and sd0 stayed unusable until reboot)
  - the operator reinserts the card; mount /dev/sd0a, list /mnt and unmount
    must succeed, which shows the driver closed and initialized the card again
  - a block descriptor held open across a mount and an unmount must keep
    reading: the unmount does not close a device a descriptor still names

This script was written without a board run; the tree records no result for
it (sys/arch/rp2040/doc/PICOCALC.md, SD card on SPI0).
"""

import glob
import os
import re
import sys
import time

ESC = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\r")
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
OPT_IN_VARIABLE = "DISCOBSD_BOARD_SD_DETACH"
OPT_IN_VALUE = "run"


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

    def run(self, command, wait=6):
        self.port.write(command.encode() + b"\r")
        out = self.read(wait)
        return [
            line for line in out.split("\n")[1:] if line.strip() and not line.startswith("discobsd")
        ]


def open_console(serial_module):
    start = time.time()
    while time.time() - start < 30 and not glob.glob(PORT_PATTERN):
        time.sleep(0.5)
    time.sleep(1)
    ports = glob.glob(PORT_PATTERN)
    if not ports:
        sys.exit("no DiscoBSD console under /dev/serial/by-id")
    console = Console(serial_module, ports[0])
    console.read(2)
    console.port.write(b"operator\r")
    console.read(4)
    return console


def status(console, command, label):
    """Run command and report whether its exit status line reads rc=0."""
    got = console.run(command + "; echo rc=$?")
    ok = bool(got) and got[-1] == "rc=0"
    print("%s: %s %s" % (label, got, "ok" if ok else "WRONG, expected rc=0"))
    return ok


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    import serial

    console = open_console(serial)
    results = []
    results.append(status(console, "mount /dev/sd0a /mnt", "mount read-write"))
    results.append(status(console, "echo probe > /mnt/probe", "dirty the mount"))
    input("Pull the SD card now, wait one second, then press Return. ")
    results.append(status(console, "umount /mnt", "umount after the card was pulled"))
    input("Insert the SD card, wait one second, then press Return. ")
    results.append(status(console, "mount /dev/sd0a /mnt", "mount the reinserted card"))
    results.append(status(console, "ls /mnt", "list the reinserted card"))
    results.append(status(console, "umount /mnt", "umount the reinserted card"))
    # A descriptor on sd0a stays usable across a mount and an unmount: the
    # subshell holds fd 3 open while the mount cycle runs, then reads it.
    results.append(
        status(
            console,
            "(exec 3</dev/sd0a; mount /dev/sd0a /mnt && umount /mnt && "
            "dd bs=1k count=1 <&3 >/dev/null)",
            "descriptor survives mount and umount",
        )
    )
    bad = results.count(False)
    print("sd detach: %s" % ("PASS" if bad == 0 else "FAIL (%d wrong)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
