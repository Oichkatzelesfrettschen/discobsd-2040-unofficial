#!/usr/bin/env python3
"""Board test for the XIP cache and bus fabric counters under machdep.

usage: DISCOBSD_BOARD_HWCOUNT=run PYTHON board_hwcount.py [unix.uf2,flash.uf2]
The script touches the board only when DISCOBSD_BOARD_HWCOUNT is exactly
"run"; unset, empty and every other value exit without opening a port or
calling discobsd-flash. With the variable set it flashes the named images
with discobsd-flash when given, logs in as operator on the USB console,
becomes root and checks that:

  - machdep.xip_acc advances across a read of a root file and machdep.xip_hit
    stays at or below it (RP2040 datasheet 2.6.3.1);
  - a write clears an XIP counter;
  - every bus_perfsel accepts events 0 to 19 and 31 and refuses 20;
  - a bus_perfctr counting all SRAM0 accesses advances, holds its value once
    its select names no event, and reads zero after a write (2.1.1.2);
  - contested SRAM0 accesses do not exceed all SRAM0 accesses.

It prints the counts it read, which are measurements of the board it ran on.
The kernel on the board must carry the machdep nodes.
"""

import glob
import os
import re
import subprocess
import sys
import time

OPT_IN_VARIABLE = "DISCOBSD_BOARD_HWCOUNT"
OPT_IN_VALUE = "run"
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
SRAM0_CONTESTED = 14
SRAM0 = 15
ROM = 19
NO_EVENT = 31
WORKLOAD = "cat /bin/sh /bin/sh /bin/sh /bin/sh > /dev/null"

ESC = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
VALUE = re.compile(r"^machdep\.\w+(?: = | -> |=)(?:-?\d+ -> )?(-?\d+)\s*$",
                   re.MULTILINE)
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
                raise SystemExit("board_hwcount: no console port") from None
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
    raise SystemExit("board_hwcount: no root shell")


def run(command, budget=30):
    """Run one command; the sentinel is split so its echo cannot match."""
    port.write((command + "; echo E''ND\r").encode())
    text = read_until(["\nEND", "\rEND"], budget).replace("\r", "\n")
    return text.split("\n", 1)[1] if "\n" in text else ""


def get(name):
    found = VALUE.findall(run(f"sysctl $M.{name}"))
    if not found:
        raise SystemExit(f"board_hwcount: no value for machdep.{name}")
    return int(found[-1]) & 0xFFFFFFFF


def put(name, value):
    """Write a node; return the shell's output, which carries any refusal."""
    return run(f"sysctl -w $M.{name}={value}")


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print(f"board_hwcount: closed; set {OPT_IN_VARIABLE}={OPT_IN_VALUE} to run")
        return 0
    if len(sys.argv) not in (1, 2):
        raise SystemExit(__doc__.split("\n\n")[1].splitlines()[0])
    global serial
    import serial

    if len(sys.argv) == 2:
        subprocess.run(["discobsd-flash", *sys.argv[1].split(",")], check=True)
        time.sleep(8)
    reopen()
    root_shell()

    put("xip_acc", 0)
    put("xip_hit", 0)
    run(WORKLOAD, 60)
    hit = get("xip_hit")
    acc = get("xip_acc")
    print(f"xip_acc={acc} xip_hit={hit} across: {WORKLOAD}")
    check(acc > 0, "xip_acc advances across a read of a root file")
    check(hit <= acc, "xip_hit does not exceed xip_acc")
    run("sleep 1")
    before = get("xip_acc")
    put("xip_acc", 0)
    after = get("xip_acc")
    check(after < before, f"a write clears xip_acc ({before} then {after})")

    for slot in range(4):
        put(f"bus_perfsel{slot}", ROM)
        low = get(f"bus_perfsel{slot}")
        put(f"bus_perfsel{slot}", 20)
        refused = get(f"bus_perfsel{slot}")
        put(f"bus_perfsel{slot}", NO_EVENT)
        none = get(f"bus_perfsel{slot}")
        check(low == ROM and refused == ROM and none == NO_EVENT,
              f"bus_perfsel{slot} takes 19 and 31 and refuses 20")

    put("bus_perfsel0", SRAM0)
    put("bus_perfsel0", NO_EVENT)
    held = get("bus_perfctr0")
    again = get("bus_perfctr0")
    check(held > 0, f"bus_perfctr0 counted SRAM0 accesses ({held})")
    check(again == held, "bus_perfctr0 holds once its select names no event")
    put("bus_perfctr0", 0)
    check(get("bus_perfctr0") == 0, "a write clears bus_perfctr0")

    put("bus_perfctr1", 0)
    put("bus_perfsel1", SRAM0_CONTESTED)
    put("bus_perfsel0", SRAM0)
    run(WORKLOAD, 60)
    put("bus_perfsel0", NO_EVENT)
    put("bus_perfsel1", NO_EVENT)
    total = get("bus_perfctr0")
    contested = get("bus_perfctr1")
    print(f"sram0={total} sram0_contested={contested} across: {WORKLOAD}")
    check(contested <= total, "contested SRAM0 accesses do not exceed all")

    print(f"board_hwcount: {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
