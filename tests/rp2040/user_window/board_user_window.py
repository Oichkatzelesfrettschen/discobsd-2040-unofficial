#!/usr/bin/env python3
"""Board test for the fixed 192-KiB user window and fork's swap reservation.

usage: DISCOBSD_BOARD_USER_WINDOW=run $PYTHON board_user_window.py \
           [unix.uf2,flash.uf2] LOG

The script touches the board only when DISCOBSD_BOARD_USER_WINDOW is exactly
"run"; unset, empty and every other value exit without opening a port or
calling discobsd-flash. With the variable set it flashes the named images
with discobsd-flash, records the boot console, logs in as operator, becomes
root with su, and stages tests/rp2040/user_window/windowtest,
tests/rp2040/fork_refusal/forktest and usr.sbin/talloc/talloc as uuencoded
text. It then checks, in order:

1. mputest (usr.bin/mputest,
   built against the same USER_DATA_END) faults on the first kernel RAM word
   above the window and reads the window's top word with the MPU on;
2. windowtest, a 170-KiB bss image only a window above 144 KiB admits,
   prints WINDOWTEST OK;
3. with flash swap held by talloc on /dev/temp0 to temp2, forktest's fork returns
   ENOMEM (FORKTEST ENOMEM) and the shell keeps running;
4. after talloc releases every extent, forktest's child sees the
   parent's bytes (FORKTEST CHILD OK);
5. windowtest still passes and the shell still answers.

Every console byte goes to LOG. The exit status is 0 when every check
passed, 1 when one failed and 2 when the console stopped answering.
"""

import binascii
import glob
import os
import re
import subprocess
import sys
import time

OPT_IN_VARIABLE = "DISCOBSD_BOARD_USER_WINDOW"
OPT_IN_VALUE = "run"
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
ESC = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\r")
SENTINEL = "__DONE__"
TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
STAGED = (
    ("windowtest", "tests/rp2040/user_window/windowtest"),
    ("forktest", "tests/rp2040/fork_refusal/forktest"),
    ("talloc", "usr.sbin/talloc/talloc"),
)
# talloc needs one contiguous run, and the free runs left by the boot's
# processes differ between boots, so each unit takes the largest run it is
# granted, found by binary search below HOLD_LIMIT blocks (each probe is
# one short command, and the console tolerates few typed commands). Three
# units soak the largest runs until none left holds forktest's roughly
# 105-block image.
HOLD_UNITS = ("/dev/temp0", "/dev/temp1", "/dev/temp2")
HOLD_LIMIT = 380


def hold_largest(unit):
    """Hold the largest extent unit is granted; return its blocks, or 0."""
    low, high, best = 1, HOLD_LIMIT, 0
    while low <= high:
        middle = (low + high) // 2
        if run("/tmp/talloc %s %d; echo rc=$?" % (unit, middle)).count("rc=0") == 1:
            best, low = middle, middle + 1
        else:
            high = middle - 1
    if best and run("/tmp/talloc %s %d; echo rc=$?" % (unit, best)).count("rc=0") != 1:
        return 0
    return best


port = None
log = None


class ConsoleSilent(Exception):
    """The console produced no sentinel inside the allowed window."""


def open_port():
    """Open the USB console, waiting through a re-enumeration."""
    global port
    import serial

    deadline = time.time() + 60
    while True:
        try:
            port = serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0)
            return
        except (IndexError, serial.SerialException):
            if time.time() > deadline:
                raise
            time.sleep(1)


def read_for(seconds, until=None):
    """Read console text for up to seconds, stopping early at until."""
    import serial

    buf = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            data = port.read(4096)
        except serial.SerialException:
            open_port()
            continue
        if data:
            buf += data
            log.write(data)
            log.flush()
            if until is not None and until.search(ESC.sub(b"", buf).decode("latin1")):
                break
        else:
            time.sleep(0.05)
    return ESC.sub(b"", buf).decode("latin1")


def send(text, per_byte=0.02):
    """Type text one byte at a time, as the line editor requires."""
    for byte in text.encode():
        port.write(bytes([byte]))
        time.sleep(per_byte)


def run(command, seconds=20):
    """Run one shell command and return its output lines before the sentinel."""
    send("%s; echo %s\r" % (command, "__DO''NE__"))
    out = read_for(seconds, re.compile(r"(?m)^" + SENTINEL + r"\s*$"))
    if not re.search(r"(?m)^" + SENTINEL + r"\s*$", out):
        raise ConsoleSilent(command)
    out = re.split(r"(?m)^" + SENTINEL + r"\s*$", out)[0]
    lines = out.split("\n")[1:]
    return [line.strip() for line in lines if line.strip()]


CHUNK_LINES = 20


def send_part(part, text):
    """Write one chunk of uuencoded lines through cat; return its byte count on the board."""
    send("cat > %s\r" % part)
    time.sleep(1)
    for line in text.splitlines():
        send(line + "\r", per_byte=0.003)
        time.sleep(0.05)
    time.sleep(0.5)
    port.write(b"\x04")
    read_for(3)
    got = run("wc -c < %s" % part, 30)
    return int(got[-1]) if got and got[-1].isdigit() else -1


def stage(name, path):
    """Copy one a.out to /tmp as uuencoded parts, join them and compare its cksum.

    No shell command line carries encoded text: cat reads each part, so the
    shell's line editor and its input limit never see the data, and each
    part's byte count is compared before the next one is sent.
    """
    with open(os.path.join(TOP, path), "rb") as handle:
        blob = handle.read()
    lines = ["begin 755 %s" % name]
    lines += [
        binascii.b2a_uu(blob[i : i + 45]).decode().rstrip("\n") for i in range(0, len(blob), 45)
    ]
    lines += ["`", "end"]
    run("rm -f /tmp/%s /tmp/%s.uu /tmp/%s.p*" % (name, name, name))
    parts = []
    for n in range(0, len(lines), CHUNK_LINES):
        text = "\n".join(lines[n : n + CHUNK_LINES]) + "\n"
        part = "/tmp/%s.p%03d" % (name, n // CHUNK_LINES)
        for _attempt in range(3):
            try:
                got = send_part(part, text)
            except ConsoleSilent:
                port.write(b"\x04\x03")
                read_for(3)
                got = -1
            if got == len(text):
                break
            print("stage %s part %s: %d of %d bytes, resending" % (name, part, got, len(text)))
        else:
            return False
        parts.append(part)
    # The console drops a command line longer than about 128 bytes, so the
    # parts join through a glob, which sorts their zero-padded names.
    run("cat /tmp/%s.p* > /tmp/%s.uu" % (name, name), 30)
    run("cd /tmp && uudecode /tmp/%s.uu" % name, 60)
    want = subprocess.run(["cksum"], input=blob, capture_output=True, check=True).stdout.split()[:2]
    got = run("cksum < /tmp/%s" % name)
    # Each staged file leaves its parts and their join in /tmp, and the
    # root file system has too few free inodes to keep those of four files.
    # A full inode table also stops the shell's pipes, so the parts go once
    # the decoded file is measured.
    run("rm -f /tmp/%s.uu /tmp/%s.p*" % (name, name))
    have = got[-1].split()[:2] if got else []
    ok = [w.decode() for w in want] == have
    print(
        "stage %-10s %d bytes cksum %s" % (name, len(blob), "match" if ok else "MISMATCH %r" % got)
    )
    return ok


def check(label, condition, results):
    print("%s %s" % ("PASS" if condition else "FAIL", label))
    results.append(condition)


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    if len(sys.argv) != 3:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    global log
    log = open(sys.argv[2], "ab")
    results = []
    subprocess.check_call(["discobsd-flash", *sys.argv[1].split(",")])
    time.sleep(3)
    open_port()
    read_for(40, re.compile(r"login: ?$", re.M))
    try:
        send("operator\r")
        read_for(8)
        run("echo login")
        send("su\r")
        read_for(4)
        check("su reaches a root shell", "0" in run("id -u"), results)
        mpu = run("mputest; echo rc=$?", 60)
        print("\n".join(mpu))
        check(
            "mputest faults above the window and reads its top",
            any(line.startswith("MPUTEST OK (mpu on)") for line in mpu) and "rc=0" in mpu,
            results,
        )
        for name, path in STAGED:
            check("staged %s" % name, stage(name, path), results)
        out = run("/tmp/windowtest; echo rc=$?", 60)
        print("\n".join(out))
        check(
            "windowtest admits and fills 170 KiB", "WINDOWTEST OK" in out and "rc=0" in out, results
        )
        held = []
        for unit in HOLD_UNITS:
            blocks = hold_largest(unit)
            if blocks:
                held.append((unit, blocks))
        print("talloc holds", held)
        check("talloc holds flash swap", bool(held), results)
        out = run("/tmp/forktest; echo rc=$?", 90)
        print("\n".join(out))
        check(
            "fork refuses with ENOMEM under a held extent",
            "FORKTEST ENOMEM" in out and "rc=0" in out,
            results,
        )
        check("shell answers after the refusal", "alive" in run("echo alive"), results)
        released = [
            run("/tmp/talloc %s 0; echo rc=$?" % unit).count("rc=0") == 1 for unit, _ in held
        ]
        check("talloc releases every extent", bool(released) and all(released), results)
        out = run("/tmp/forktest; echo rc=$?", 180)
        print("\n".join(out))
        check(
            "forked child sees the parent image",
            "FORKTEST CHILD OK" in out and "rc=0" in out,
            results,
        )
        out = run("/tmp/windowtest; echo rc=$?", 90)
        check("windowtest passes after the fork", "WINDOWTEST OK" in out and "rc=0" in out, results)
        check("shell answers at the end", "alive" in run("echo alive"), results)
    except ConsoleSilent as silent:
        tail = read_for(5)
        print("console silent after %r; last output:\n%s" % (silent.args[0], tail[-600:]))
        return 2
    print("%d of %d checks passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
