#!/usr/bin/env python3
"""Board test for core 1 as the kernel's observer (sys/arch/rp2040/doc/CORE1.md).

usage: DISCOBSD_BOARD_CORE1=run PYTHON board_core1.py unix.elf [unix.uf2,flash.uf2]
The script touches the board only when DISCOBSD_BOARD_CORE1 is exactly "run";
unset, empty and every other value exit without opening a port or calling
discobsd-flash. With the variable set it flashes the named images with
discobsd-flash when given, then logs in as operator on the USB console,
becomes root and checks, against the symbol addresses of unix.elf:

  - core 1 is running ten seconds after boot with no restart, stall or fault;
  - a 300 ms masked spin (machdep.core1_stall_ms) is counted as one stall
    whose probed PC lies in core1_stall and whose LR lies in core1_sysctl;
  - flash writes and swapping add no stall;
  - a parked core 1 counts nothing, and a relaunched one counts again;
  - reboot leaves no stall record;
  - a 6 s masked spin makes the board reset itself, and the next boot
    reports watchdog reason 2, site 3 and the same PC and LR.

The last step resets the board with its file systems synced. The kernel on
the board must be the one unix.elf describes.
"""

import glob
import os
import re
import subprocess
import sys
import time

OPT_IN_VARIABLE = "DISCOBSD_BOARD_CORE1"
OPT_IN_VALUE = "run"
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"

ESC = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
VALUE = re.compile(r"^machdep\.(\w+)=(-?\d+)\s*$", re.MULTILINE)
port = None
failures = 0


def check(condition, message):
    global failures
    print(("PASS " if condition else "FAIL ") + message)
    if not condition:
        failures += 1


def reopen(budget=90):
    """Open the console, waiting for the board to enumerate."""
    global port
    deadline = time.time() + budget
    while True:
        try:
            port = serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0.2)
            return
        except (IndexError, serial.SerialException):
            if time.time() > deadline:
                raise SystemExit("board_core1: no console port") from None
            time.sleep(1)


def read_until(needles, budget):
    text = ""
    deadline = time.time() + budget
    while time.time() < deadline:
        try:
            data = port.read(4096)
        except serial.SerialException:
            break
        text += ESC.sub("", data.decode("latin1"))
        if any(needle in text for needle in needles):
            break
    return text


def root_shell():
    """Reach a root prompt from whatever the console shows."""
    for _ in range(10):
        port.write(b"\r")
        tail = read_until(["login:", "$ ", "# "], 30).rstrip()[-40:]
        if tail.endswith("#"):
            port.write(b"M=machdep\r")
            read_until(["# "], 10)
            return
        if tail.endswith("$"):
            port.write(b"su\r")
            read_until(["# "], 10)
        elif "login:" in tail:
            port.write(b"operator\r")
            read_until(["$ "], 20)
    raise SystemExit("board_core1: no root shell")


def run(command, budget=30):
    """Run one command; the sentinel is split so its echo cannot match."""
    port.write((command + "; echo E''ND\r").encode())
    return read_until(["\nEND", "\rEND"], budget)


def values(*names):
    """Read machdep nodes, a few per command so the line never wraps."""
    found = {}
    for name in names:
        found.update(
            (key, int(value) & 0xFFFFFFFF)
            for key, value in VALUE.findall(
                run(f"sysctl $M.{name}").replace("\r", "\n")
            )
        )
    missing = [name for name in names if name not in found]
    if missing:
        raise SystemExit(f"board_core1: no value for {missing}")
    return found


def symbol_ranges(elf):
    listing = subprocess.run(
        ["arm-none-eabi-nm", "-S", elf], check=True, capture_output=True, text=True
    ).stdout
    ranges = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] in ("core1_stall", "core1_sysctl"):
            start = int(fields[0], 16)
            ranges[fields[3]] = (start, start + int(fields[1], 16))
    if len(ranges) != 2:
        raise SystemExit(f"board_core1: {elf} lacks core1_stall or core1_sysctl")
    return ranges


def inside(address, span):
    return span[0] <= (address & ~1) < span[1]


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print(f"board_core1: closed; set {OPT_IN_VARIABLE}={OPT_IN_VALUE} to run")
        return 0
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__.split("\n\n")[1].splitlines()[0])
    global serial
    import serial

    spans = symbol_ranges(sys.argv[1])
    if len(sys.argv) == 3:
        subprocess.run(["discobsd-flash", *sys.argv[2].split(",")], check=True)
        time.sleep(8)
    reopen()
    root_shell()

    run("sleep 10", 20)
    state = values("core1", "core1_restarts", "core1_stalls", "core1_faults")
    check(state["core1"] == 1, "core 1 is running ten seconds after boot")
    check(state["core1_restarts"] == 0 and state["core1_faults"] == 0,
          "no restart and no core 1 fault")
    base = state["core1_stalls"]

    run("sysctl -w $M.core1_stall_ms=300")
    state = values("core1_stalls", "core1_gap_us", "core1_pc", "core1_lr",
                   "core1_restarts")
    check(state["core1_stalls"] == base + 1, "a 300 ms spin is one stall")
    check(250000 <= state["core1_gap_us"] <= 320000,
          f"its length reads {state['core1_gap_us']} us")
    check(inside(state["core1_pc"], spans["core1_stall"]),
          f"the probed PC {state['core1_pc']:#x} lies in core1_stall")
    check(inside(state["core1_lr"], spans["core1_sysctl"]),
          f"the probed LR {state['core1_lr']:#x} lies in core1_sysctl")
    check(state["core1_restarts"] == 0, "the stall restarted nothing")

    run("dd if=/dev/zero of=/tmp/c1 bs=1k count=128; sync; rm /tmp/c1; sync", 180)
    run("ls -lR /bin /etc | sort | wc -l", 120)
    state = values("core1_stalls", "core1_restarts", "core1_faults")
    check(state["core1_stalls"] == base + 1,
          "flash writes and swapping add no stall")
    check(state["core1_restarts"] == 0 and state["core1_faults"] == 0,
          "and no restart or fault")

    run("sysctl -w $M.core1=0; sleep 3")
    run("sysctl -w $M.core1_stall_ms=300")
    state = values("core1", "core1_stalls")
    check(state["core1"] == 0 and state["core1_stalls"] == base + 1,
          "a parked core 1 counts nothing")
    run("sysctl -w $M.core1=1; sleep 5", 20)
    run("sysctl -w $M.core1_stall_ms=300")
    state = values("core1", "core1_stalls", "core1_restarts")
    check(state["core1"] == 1 and state["core1_stalls"] == base + 2
          and state["core1_restarts"] == 0, "a relaunched core 1 counts again")

    port.write(b"sync; reboot\r")
    time.sleep(12)
    port.close()
    reopen()
    root_shell()
    run("sleep 6", 15)
    state = values("watchdog_site", "core1", "core1_stalls")
    check(state["watchdog_site"] == 0 and state["core1_stalls"] == 0,
          "reboot leaves no stall record")
    check(state["core1"] == 1, "core 1 runs after the reboot")

    port.write(b"sync; sync; sysctl -w $M.core1_stall_ms=6000\r")
    time.sleep(15)
    port.close()
    reopen()
    root_shell()
    run("sleep 6", 15)
    state = values("watchdog_reason", "watchdog_site", "watchdog_arg",
                   "watchdog_arg2", "core1")
    check(state["watchdog_reason"] == 2 and state["watchdog_site"] == 3,
          "a 6 s spin reset the board with the stall site on record")
    check(inside(state["watchdog_arg"], spans["core1_stall"]),
          f"the recorded PC {state['watchdog_arg']:#x} lies in core1_stall")
    check(inside(state["watchdog_arg2"], spans["core1_sysctl"]),
          f"the recorded LR {state['watchdog_arg2']:#x} lies in core1_sysctl")
    check(state["core1"] == 1, "core 1 runs after the reset")

    print(f"board_core1: {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
