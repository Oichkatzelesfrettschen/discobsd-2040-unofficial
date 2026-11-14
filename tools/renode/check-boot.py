#!/usr/bin/env python3
"""Boot a UART0-console kernel under Renode and assert on its console.

Renode runs with its monitor on stdin (renode_monitor.py), so the gate holds
no listening socket. UART0's output goes to a file backend; this driver
advances virtual time in slices and matches the expected console lines in
order against that file, and types into UART0 through the model's
WriteChar. Virtual time is the budget, so a slow host lengthens the run
without failing it; WALL_SECONDS bounds a hung emulator.

Stages, each a run of lines that must arrive in order:

probe
    Reaching "swap size" means boot2 configured the SSI for XIP and jumped
    to 0x10000100, SystemClock_Config finished rather than spinning,
    rom_func_lookup() resolved the erase and program pointers through the
    real boot ROM table, Dhara resumed its journal through them, and the
    partition and swap geometry came back the size the image was built
    with. The sizes are asserted rather than only the line names, so a
    change to the flash layout that nothing else notices fails here.
login
    fsck's summary, the daemons and getty's prompt: exec of a compressed
    a.out through the swap writer, a tty a process can open, fork and wait.
shell
    A login and commands, because a prompt alone would also appear from a
    shell that cannot execute anything: uname, the process list with the
    swapped-out init's time and name (check_ps()), the capacity record, the
    image identity, the MPU fault test and the exec residue probe. The
    identity lines are computed here from UNIX_BIN (image_identity()): the
    emulated kernel summed the flash model's bytes at boot, so its length,
    its sum and the word tools/kernsum stamped must all equal what the file
    on the host holds.

Slicing virtual time serializes the CPU thread and the SSI model's clocking
thread about as much as the terminal tester's pauses did, so this gate does
not decide SSI concurrency either; sys/arch/rp2040/doc/research/emulation.md
carries the free-running measurement that does.

CONFIG names the kernel configuration the banner must carry and defaults to
PICO_UART. PICOCALC_PSRAM is the union kernel booted with a bare-Pico UART0
descriptor (check-boot.sh patches it in). The probe stage asserts the lines
the descriptor decides: the console device it names, and for the union the
USB attach beside it and the absence of every PicoCalc device line, since
that kernel links the panel, keyboard, PSRAM and SD drivers and only the
descriptor keeps them from probing pins a bare Pico leaves floating.

Usage: check-boot.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR [CONFIG]
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from renode_monitor import Renode, RenodeError  # noqa: E402

SLICE_SECONDS = 0.5
PROBE_BUDGET_SECONDS = 20.0
LOGIN_BUDGET_SECONDS = 180.0
COMMAND_BUDGET_SECONDS = 60.0
WALL_SECONDS = 3300.0

CPU_LINES = [
    "cpu: Cortex-M0+, ARMv6-M, no MMU",
    "cpu: 125 MHz core, 125 MHz peripheral",
    # mpu.c prints what MPU_TYPE and MPU_CTRL read back after it programmed
    # the map: eight regions from the datasheet, the four the map holds,
    # ENABLE and PRIVDEFENA set.
    "mpu: 8 regions, 4 programmed, MPU_CTRL 0x5: rom 16K r-x, user 192K rwx, sio div rw",
    # A cold start under the emulator reports neither a watchdog reason nor
    # a masked site; a board returning from picotool reboot -f reports
    # reason 2.
    "watchdog: reason 0, last masked site 0 arg 0",
]

# The console and board devices the descriptor selects, in attach order.
# PICO_UART links no USB driver. The union kernel attaches uartusb on a bare
# Pico whichever console the descriptor names; its controller is unmodeled
# (union-warning-classes.txt), so the line shows the attach returned instead
# of waiting on a register the model never serves.
BOARD_LINES = {
    "PICO_UART": [
        "uart0: pins tx=GP0/rx=GP1, console",
    ],
    "PICOCALC_PSRAM": [
        "uart0: pins tx=GP0/rx=GP1, console",
        "uartusb: CDC-ACM on the USB device controller, interrupt 5",
    ],
}

# Device lines a bare-Pico descriptor must suppress, matched at the start of
# a console line: lcdcons.c prints lcd0 and kbd0, psram.c psram0 and sd.c sd0
# only when board_is_picocalc() holds.
ABSENT_PREFIXES = {
    "PICO_UART": (),
    "PICOCALC_PSRAM": ("lcd0:", "kbd0:", "psram0:", "sd0:"),
}

STORAGE_LINES = [
    # fl0 prints only after rom_func_lookup() has resolved the ROM's flash
    # entry points and Dhara has resumed its journal through them.
    "fl0: 989 kbytes on QSPI flash, 1536 kbytes raw",
    "fl0a: partition type b7, sector 2, size 988 kbytes",
    "fl1: 384 kbytes raw QSPI flash for swap",
    "phys mem  = 264 kbytes",
    "user mem  = 192 kbytes",
    "root dev  = (0,1)",
    "swap dev  = (0,8)",
    "root size = 988 kbytes",
    "swap size = 380 kbytes",
]

# fsck's summary counts follow the manifest and move whenever a program is
# added or dropped, so only the line's start is asserted; the geometry is the
# probe stage's job.
LOGIN_LINES = [
    "Automatic boot in progress: starting file system checks.",
    "/dev/fl0a:",
]

# The formatter accepts the versioned kernel structure only when its compiled
# size agrees. These stable fragments pin every fixed-table limit and the
# u-area geometry without treating one emulator workload's dynamic peaks as
# board headroom.
# The u-area row of kern.capacity: kernel stack peak use, the least free
# space seen, and whether the stack ever reached its end.
UAREA_ROW = re.compile(r"^uarea bytes \d+ user \d+ stack \d+ peak (\d+) "
                       r"min-free (\d+) saturated (\d+)$", re.M)

CAPACITY_LINES = [
    "kern.capacity=version 1 size 100",
    "proc limit 16 current",
    "inode limit 24 current",
    "file limit 18 current",
    "clist limit 32 current",
    "buffer limit 4 current",
    "uarea bytes 3072 user 1024 stack 2048 peak",
]

# The identity words of a flash image, as tools/kernsum/kernsum.c lays them
# out: the length and CRC32 in vector entries 12 and 13 of the table at
# 0x10000100, and the address of the board descriptor in the word after the
# 48-entry table. The sum reads four spans as zero: the second stage, the
# stamp the PicoCalc UF2 loader writes over vector entries 4 to 10, the CRC
# word and the board descriptor.
IMAGE_ADDRESS = 0x10000000
IDENTITY_OFFSET = 0x130
BOARD_WORD_OFFSET = 0x1C0
BOOT2_SIZE = 0x100
STAMP_OFFSET = 0x110
STAMP_SIZE = 0x1C
BOARD_DESCRIPTOR_SIZE = 16


def identity_sum(image: bytes) -> int:
    """Return the CRC32 image_identify() computes over a kernel holding image."""
    work = bytearray(image)
    board = struct.unpack_from("<I", work, BOARD_WORD_OFFSET)[0] - IMAGE_ADDRESS
    work[:BOOT2_SIZE] = bytes(BOOT2_SIZE)
    work[STAMP_OFFSET:STAMP_OFFSET + STAMP_SIZE] = bytes(STAMP_SIZE)
    work[IDENTITY_OFFSET + 4:IDENTITY_OFFSET + 8] = bytes(4)
    work[board:board + BOARD_DESCRIPTOR_SIZE] = bytes(BOARD_DESCRIPTOR_SIZE)
    return zlib.crc32(work)


def image_identity(path: str) -> list[str]:
    """Return the three machdep.image lines a kernel running PATH prints."""
    image = open(path, "rb").read()
    length, stored = struct.unpack_from("<2I", image, IDENTITY_OFFSET)
    # sysctl prints an int, so a sum with the top bit set reads negative.
    crc = struct.unpack("<i", struct.pack("<I", identity_sum(image)))[0]
    stored = struct.unpack("<i", struct.pack("<I", stored))[0]
    if length != len(image) or stored != crc:
        raise ValueError(f"{path}: identity words do not describe the file")
    return [
        f"machdep.image_length={length}",
        f"machdep.image_crc={crc}",
        f"machdep.image_crc_stored={stored}",
    ]


CORE1_LINES = [
    "machdep.core1=1",
    "machdep.core1_restarts=0",
    "machdep.core1_faults=0",
    "machdep.core1_stalls=0",
]

STALL_LINES = [
    "machdep.core1_stalls=1",
    "machdep.core1_restarts=0",
]

MEMORY_DEVICES = ("mem", "kmem", "swap")

# The BSD ps row of init: the pid, a tty field, minutes and seconds of
# processor time, and the command. init belongs to root, so the kernel
# withholds its arguments from operator and ps prints the command name in
# brackets.
PS_INIT_ROW = re.compile(r"^ *1 \S+ +\d+:\d\d +\[\S*init\]$", re.M)

# The caller's own ps row in the default BSD listing: pid, terminal, time,
# and the command with its arguments.
PS_OWN_ROW = re.compile(r"^ *\d+ co +\d+:\d\d ps$", re.M)

# "ps -o pid= -o stime= -o etime= -p 0": the swapper's row, with no start.
PS_SWAPPER_START = re.compile(r"^ *0 +- +-$", re.M)

# "fstat -v -p 1" as operator: the refusal fstat names, and any file row
# of pid 1, which must be absent. The console expands the tabs fstat
# prints, so columns are matched as runs of spaces.
FSTAT_REFUSED = re.compile(r"^fstat: pid 1: \S.*$", re.M)
FSTAT_INIT_ROW = re.compile(r"^\S+ +\S+ +1 +(wd|\d+) +\d+, ", re.M)

# One row of "ps -A -o pid= -o comm=": every header is empty, so ps prints
# no header line.
PS_ROW = re.compile(r"^ *(\d+) (\S.*)$")

# The processes a booted operator shell always has, by pid or by the last
# path component of the command name: the swapper, init, update, the
# login shell and ps itself. Each lives in a different place -- resident,
# on flash or in the pool -- when ps runs, so a reader that loses any
# residency state loses a row here.
PS_REQUIRED = (
    ("pid 0", lambda pid, name: pid == 0 and name == "swapper"),
    ("init", lambda pid, name: pid == 1 and name == "init"),
    ("update", lambda pid, name: name == "update"),
    ("the operator shell", lambda pid, name: name.lstrip("-") == "sh"),
    ("ps itself", lambda pid, name: name == "ps"),
)

ESCAPE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


class StageFailed(Exception):
    """A stage's expected text never reached the console; later stages cannot run."""


class Console:
    """UART0's transcript, read from the file backend, and the keys typed into it."""

    def __init__(self, renode: Renode, path: str) -> None:
        self.renode = renode
        self.path = path
        self.virtual = 0.0
        self.cursor = 0
        self.failures: list[str] = []

    def text(self) -> str:
        try:
            with open(self.path, encoding="utf-8", errors="replace") as handle:
                raw = handle.read()
        except FileNotFoundError:
            return ""
        return ESCAPE.sub("", raw).replace("\r", "")

    def advance(self) -> None:
        self.renode.command(f'emulation RunFor "{SLICE_SECONDS}"')
        self.virtual += SLICE_SECONDS

    def wait_for(self, needle: str, what: str, budget: float) -> None:
        """Wait for needle after the previous match; matching moves the cursor past it."""
        spent = 0.0
        while True:
            text = self.text()
            found = text.find(needle, self.cursor)
            if found >= 0:
                self.cursor = found + len(needle)
                return
            if spent >= budget:
                break
            self.advance()
            spent += SLICE_SECONDS
        tail = text[self.cursor:][-400:]
        self.fail(f"{what}: {needle!r} not on UART0 after {self.virtual:.1f} "
                  f"virtual seconds; unmatched tail {tail!r}")
        raise StageFailed(what)

    def wait_lines(self, lines: list[str], what: str, budget: float) -> None:
        for line in lines:
            self.wait_for(line, what, budget)
        print(f"check-renode: {what} after {self.virtual:.1f} virtual seconds")

    def absent_before_cursor(self, prefixes: tuple[str, ...], what: str) -> None:
        """Fail when a line already matched past starts with one of prefixes."""
        seen = [line for line in self.text()[:self.cursor].splitlines()
                if prefixes and line.startswith(prefixes)]
        if seen:
            self.fail(f"{what}: {seen[0]!r} on UART0; the descriptor selects a board "
                      "without that device")
            raise StageFailed(what)

    def type_line(self, line: str) -> None:
        for byte in (line + "\r").encode():
            self.renode.command(f"sysbus.uart0 WriteChar {byte:#04x}")

    def fail(self, message: str) -> None:
        print(f"check-renode: FAIL {message}", file=sys.stderr)
        self.failures.append(message)


# The configurations that link the core 1 observer (the "core1" option in
# their Config). The union kernel carries no core1.c, so its machdep.core1
# nodes do not exist and the core 1 stages have nothing to read.
CORE1_CONFIGS = ("PICO_UART",)


def check_ps(console: Console) -> None:
    """Require a ps row for every process, wherever its image lives.

    ps reads the kernel's process records (KERN_PROC), which the kernel
    fills for a resident process, an image on the swap device and one in
    the SwapRAM pool, so operator runs it without privilege and no row is
    dropped. The POSIX listing must hold every process of PS_REQUIRED
    exactly once; the BSD listing must hold init with its time.
    """
    start = console.cursor
    console.type_line("ps -A -o pid= -o comm=; echo ps-end")
    console.wait_for("\nps-end", "process list", COMMAND_BUDGET_SECONDS)
    lines = console.text()[start:console.cursor].splitlines()
    rows = [(int(m.group(1)), m.group(2).rsplit("/", 1)[-1])
            for m in map(PS_ROW.match, lines) if m]
    for what, wanted in PS_REQUIRED:
        found = [row for row in rows if wanted(*row)]
        if len(found) != 1:
            console.fail(f"process list: {len(found)} rows for {what} in {lines!r}")
            raise StageFailed("process list")
    if len({pid for pid, _ in rows}) != len(rows):
        console.fail(f"process list: a pid appears twice in {lines!r}")
        raise StageFailed("process list")
    console.wait_for("$", "operator shell after the process list", COMMAND_BUDGET_SECONDS)

    start = console.cursor
    console.type_line("ps ax; echo ps-end")
    console.wait_for("\nps-end", "BSD process list", COMMAND_BUDGET_SECONDS)
    rows_text = console.text()[start:console.cursor]
    if not PS_INIT_ROW.search(rows_text):
        console.fail(f"BSD process list: no row for init with a time and its name in {rows_text!r}")
        raise StageFailed("BSD process list")
    console.wait_for("$", "operator shell after the BSD process list", COMMAND_BUDGET_SECONDS)

    # With no selection, both grammars list the caller's processes on the
    # caller's terminal. The shell sits on the console, whose sessions carry
    # the device the redirector substitutes, so a ps that matched the
    # terminal by /dev/console's own number would list nothing here.
    start = console.cursor
    console.type_line("ps; ps -o pid= -o comm=; echo ps-end")
    console.wait_for("\nps-end", "default process lists", COMMAND_BUDGET_SECONDS)
    lines = console.text()[start:console.cursor].splitlines()
    if not PS_OWN_ROW.search("\n".join(lines)):
        console.fail(f"default process lists: no BSD row for ps itself in {lines!r}")
        raise StageFailed("default process lists")
    if not any(m and m.group(2) == "ps" for m in map(PS_ROW.match, lines)):
        console.fail(f"default process lists: no POSIX row for ps itself in {lines!r}")
        raise StageFailed("default process lists")
    console.wait_for("$", "operator shell after the default process lists",
                     COMMAND_BUDGET_SECONDS)

    # fork sets u_start, and the swapper never forked: its record reports
    # start 0, so its start and elapsed time print as "-" rather than as
    # times reckoned from the epoch.
    start = console.cursor
    console.type_line("ps -o pid= -o stime= -o etime= -p 0; echo ps0-end")
    console.wait_for("\nps0-end", "swapper start", COMMAND_BUDGET_SECONDS)
    text = console.text()[start:console.cursor]
    if not PS_SWAPPER_START.search(text):
        console.fail(f"swapper start: no row with '-' start and elapsed time in {text!r}")
        raise StageFailed("swapper start")
    console.wait_for("$", "operator shell after the swapper start", COMMAND_BUDGET_SECONDS)
    print(f"check-renode: process list after {console.virtual:.1f} virtual seconds")


def check_memory_devices(console: Console) -> None:
    """Require the operator shell to be refused every kernel memory device.

    /dev/mem, /dev/kmem and /dev/swap are mode 0640 root:kmem and operator
    is outside kmem, so each open fails, while check_ps lists every process
    from the same shell without them. The
    console echoes the typed loop, so a verdict line is matched with the
    device name spelled out, which the echoed "$d" never carries. The loop
    stays within ED_MAX in bin/sh/edit.c, 126 bytes, past which the shell's
    line editor rings the bell and drops the rest of the line.
    """
    start = console.cursor
    console.type_line("for d in mem kmem swap; do (:</dev/$d) 2>/dev/null"
                      " && echo $d-open || echo $d-shut; done; echo dev-end")
    console.wait_for("\ndev-end", "kernel memory devices", COMMAND_BUDGET_SECONDS)
    output = console.text()[start:console.cursor]
    for device in MEMORY_DEVICES:
        if f"\n{device}-shut" not in output:
            console.fail(f"kernel memory devices: operator open of /dev/{device} was not refused "
                         f"in {output!r}")
            raise StageFailed("kernel memory devices")
    console.wait_for("$", "operator shell after the device opens", COMMAND_BUDGET_SECONDS)
    print(f"check-renode: kernel memory devices refused after {console.virtual:.1f} "
          "virtual seconds")


def check_fstat(console: Console) -> None:
    """Require fstat to list the operator shell's files and no one else's.

    fstat reads the kernel's KERN_PROC and KERN_FILE records, so operator,
    whom check_memory_devices showed outside every memory device, runs it.
    While fstat runs the shell is swapped out -- to the swap device in the
    PICO_UART kernel and to the SwapRAM pool in the union kernel -- so the
    kernel reads the shell's descriptor table through the user-area reader.
    The shell's working directory and its standard input, the console, must
    each appear on a row of their own. init belongs to root, so the kernel
    refuses its descriptors and fstat -v names the refusal with no row.
    """
    start = console.cursor
    console.type_line("echo sh=$$; fstat -p $$; echo fs-end")
    console.wait_for("\nfs-end", "fstat of the shell", COMMAND_BUDGET_SECONDS)
    output = console.text()[start:console.cursor]
    shell = re.search(r"^sh=(\d+)$", output, re.M)
    if shell is None:
        console.fail(f"fstat of the shell: no shell pid in {output!r}")
        raise StageFailed("fstat of the shell")
    for fd, kind in (("wd", "dir"), ("0", "chr")):
        row = re.compile(rf"^operator +\S+ +{shell.group(1)} +{fd} +\d+, +\d+"
                         rf" +\d+ +\d+ +{kind} $", re.M)
        if not row.search(output):
            console.fail(f"fstat of the shell: no {fd} row of type {kind} in {output!r}")
            raise StageFailed("fstat of the shell")
    console.wait_for("$", "operator shell after fstat", COMMAND_BUDGET_SECONDS)

    start = console.cursor
    console.type_line("fstat -v -p 1 2>&1; echo fs1-end")
    console.wait_for("\nfs1-end", "fstat of init", COMMAND_BUDGET_SECONDS)
    output = console.text()[start:console.cursor]
    if not FSTAT_REFUSED.search(output) or FSTAT_INIT_ROW.search(output):
        console.fail(f"fstat of init: operator was not refused init's files in {output!r}")
        raise StageFailed("fstat of init")
    console.wait_for("$", "operator shell after fstat of init", COMMAND_BUDGET_SECONDS)
    print(f"check-renode: fstat after {console.virtual:.1f} virtual seconds")


def check_uarea_paths(console: Console) -> None:
    """Require a signal round trip and an unsaturated u-area afterward.

    The shell catches the SIGTERM it sends itself, so the kernel builds a signal
    frame on the user stack and returns through sigreturn. By now the run
    has also taken mputest's deliberate faults and its exec, swapped images
    out and in (to the pool in the union kernel), and switched between
    processes throughout. The u-area row read last must then show the
    kernel stack's peak use with free bytes left and no saturation, the
    capacity record's account of every one of those paths.
    """
    start = console.cursor
    # bin/sh's trap takes signal numbers below MAXTRAP (26, bin/sh/defs.h)
    # only, so the stage catches 15, SIGTERM (sys/signal.h).
    console.type_line("trap 'echo sig-caught' 15; kill -15 $$; trap 15; echo sig-end")
    console.wait_for("\nsig-end", "signal round trip", COMMAND_BUDGET_SECONDS)
    if "\nsig-caught" not in console.text()[start:console.cursor]:
        console.fail("signal round trip: the shell's SIGUSR1 handler did not run")
        raise StageFailed("signal round trip")
    console.wait_for("$", "operator shell after the signal", COMMAND_BUDGET_SECONDS)

    start = console.cursor
    console.type_line("sysctl kern.capacity; echo cap-end")
    console.wait_for("\ncap-end", "final capacity record", COMMAND_BUDGET_SECONDS)
    found = UAREA_ROW.search(console.text()[start:console.cursor])
    if found is None:
        console.fail("final capacity record: no u-area row")
        raise StageFailed("final capacity record")
    peak, min_free, saturated = (int(value) for value in found.groups())
    print(f"check-renode: u-area stack peak {peak} min-free {min_free} "
          f"saturated {saturated}")
    if peak == 0 or min_free == 0 or saturated != 0:
        console.fail(f"final capacity record: u-area peak {peak} min-free {min_free} "
                     f"saturated {saturated}")
        raise StageFailed("final capacity record")
    console.wait_for("$", "operator shell after the capacity record", COMMAND_BUDGET_SECONDS)


# The clock past 2038: 4607230038 is yymmddhhmm for 2046-07-23 00:38 UTC
# (bin/date2/date.c reads a two-digit year below 70 as 20yy), 2,415,919,080
# seconds, which has bit 31 set. A program that read the kernel's count as a
# signed long would print a year before 1970.
CLOCK_SET = "date -u 4607230038"
CLOCK_ROW = re.compile(r"^\w{3} Jul 23 00:3[89]:\d\d \S+ 2046$", re.M)
# touch -t reads local time and ls -l prints a time over six months old
# with its year; the file is dated 2045-01-02, well past 2038.
TOUCHED_ROW = re.compile(r"^-\S+ .* Jan +2 +2045 /tmp/y2045$", re.M)
# A file created now is dated 2046-07-22 or -23 in the kernel's zone.
CREATED_ROW = re.compile(r"^-\S+ .* Jul 2[23] \d\d:\d\d /tmp/n2046$", re.M)
BOOTTIME_ROW = re.compile(r"^\d+$", re.M)
# A program the image's own compiler builds: Smaller C reads no header, so
# the source declares what it calls, and its long is the 32-bit count the
# members of /usr/lib/libc.a take (distrib/rp2040/boardlibc-members).
NATIVE_SOURCE = (
    "char *ctime(long *); long time(long *); int printf(char *, ...);",
    'int main(void) { long t = time(0); printf("%s", ctime(&t)); return 0; }',
)
NATIVE_ROW = re.compile(r"^\w{3} Jul 2[23] \d\d:\d\d:\d\d 2046$", re.M)
NATIVE_BUDGET_SECONDS = 240.0


def check_clock_past_2038(console: Console) -> None:
    """Set the clock to 2046 as root and read it back through the programs.

    date sets the clock with settimeofday() and reads it with
    gettimeofday() through the 64-bit wrappers, which widen the kernel's
    unsigned count. touch -t writes a 2045 file time through utimes(), and
    ls -l reads the file times back through stat(); a count that wrapped
    or was read signed shows as a year before 1970. sysctl -n prints the
    boot time as a number, which shows a question mark if a conversion
    reached printf's 64-bit directive without its digit member.
    """
    console.type_line("su")
    console.wait_lines(["#"], "root shell for the clock", COMMAND_BUDGET_SECONDS)
    start = console.cursor
    console.type_line(f"{CLOCK_SET} >/dev/null; date -u; echo clock-end")
    console.wait_for("\nclock-end", "clock past 2038", COMMAND_BUDGET_SECONDS)
    output = console.text()[start:console.cursor]
    if not CLOCK_ROW.search(output):
        console.fail(f"clock past 2038: date -u does not read 2046 in {output!r}")
        raise StageFailed("clock past 2038")
    # The shell's line editor takes about 127 characters, so the steps go
    # on lines of their own.
    console.type_line("touch -t 204501020304 /tmp/y2045; touch /tmp/n2046; echo touch-end")
    console.wait_for("\ntouch-end", "files touched past 2038", COMMAND_BUDGET_SECONDS)
    start = console.cursor
    console.type_line("ls -l /tmp/y2045 /tmp/n2046; sysctl -n kern.boottime; "
                      "echo files-end")
    console.wait_for("\nfiles-end", "file times past 2038", COMMAND_BUDGET_SECONDS)
    output = console.text()[start:console.cursor]
    for row, what in ((TOUCHED_ROW, "touch -t 2045"), (CREATED_ROW, "a file created in 2046"),
                      (BOOTTIME_ROW, "kern.boottime as a number")):
        if not row.search(output):
            console.fail(f"file times past 2038: no {what} row in {output!r}")
            raise StageFailed("file times past 2038")
    print(f"check-renode: clock and file times read 2046 after {console.virtual:.1f} "
          "virtual seconds")
    # /usr/bin/cc compiles a program that prints the clock through ctime(),
    # which links ctime.o, tzset.o, timezone.o and asctime.o from the
    # image's libc.a and reads the count unsigned.
    for index, line in enumerate(NATIVE_SOURCE):
        console.type_line(f"echo '{line}' >{'>' if index else ''}/tmp/d.c; echo line-end")
        console.wait_for("\nline-end", "native source line", COMMAND_BUDGET_SECONDS)
    start = console.cursor
    console.type_line("cc -o /tmp/d /tmp/d.c && /tmp/d; echo native-end")
    console.wait_for("\nnative-end", "program compiled on the target", NATIVE_BUDGET_SECONDS)
    output = console.text()[start:console.cursor]
    if not NATIVE_ROW.search(output):
        console.fail(f"program compiled on the target: no 2046 date in {output!r}")
        raise StageFailed("program compiled on the target")
    print("check-renode: a program compiled on the target prints 2046 through ctime() "
          f"after {console.virtual:.1f} virtual seconds")
    console.type_line("rm -f /tmp/y2045 /tmp/n2046 /tmp/d /tmp/d.c; exit")
    console.wait_for("$", "operator shell after the clock", COMMAND_BUDGET_SECONDS)


def run_stages(console: Console, config: str, identity: list[str]) -> None:
    console.wait_lines([f"DiscoBSD 2.7 ({config})"] + CPU_LINES + BOARD_LINES[config]
                       + STORAGE_LINES, "device probe", PROBE_BUDGET_SECONDS)
    console.absent_before_cursor(ABSENT_PREFIXES[config], "descriptor-suppressed devices")
    console.wait_lines(LOGIN_LINES, "file system check and daemons", LOGIN_BUDGET_SECONDS)
    # init opens /dev/console and forks getty, which needs a tty a process
    # can open rather than the kernel's own cnputc path.
    console.wait_lines(["login:"], "login prompt", LOGIN_BUDGET_SECONDS)
    # The operator account has an empty password, so the name alone reaches
    # a shell.
    console.type_line("operator")
    console.wait_lines(["$"], "shell prompt", COMMAND_BUDGET_SECONDS)
    console.type_line("uname -sr")
    console.wait_lines(["DiscoBSD 2.7"], "uname", COMMAND_BUDGET_SECONDS)
    check_ps(console)
    check_memory_devices(console)
    check_fstat(console)
    console.type_line("sysctl kern.capacity")
    console.wait_lines(CAPACITY_LINES, "capacity record", COMMAND_BUDGET_SECONDS)
    console.type_line("sysctl machdep.image_length machdep.image_crc machdep.image_crc_stored")
    console.wait_lines(identity, "image identity", COMMAND_BUDGET_SECONDS)
    # usr.bin/mputest forks children that read kernel RAM, kernel text, SIO,
    # the unstriped SRAM aliases and the XIP cache-as-SRAM window and expects
    # SIGSEGV for each while machdep.mpu.enable reads 1, and reads the boot
    # ROM and the window top without harm. "mpu on" pins the
    # state, so a kernel whose map was dropped passes its own consistency
    # check and still fails here.
    console.type_line("mputest")
    console.wait_lines(["MPUTEST OK (mpu on)"], "MPU fault test", COMMAND_BUDGET_SECONDS)
    # The residue probe fills the span between the image and its stack,
    # execs, and counts what the new image still reads there. exec_clear
    # zeroes that span, so a kernel that hands a program the last one's
    # bytes reports a surviving count and fails here.
    console.type_line("mputest residue")
    console.wait_lines(["RESIDUE OK"], "exec residue probe", COMMAND_BUDGET_SECONDS)
    check_uarea_paths(console)
    check_clock_past_2038(console)
    if config not in CORE1_CONFIGS:
        return
    # The PICO_UART kernel launches core 1 through the boot ROM a second
    # after the clock starts and pings it once a second; two unanswered
    # pings restart it. After five more seconds a running state with no
    # restart means the launch completed and the rings carried each ping
    # and its answer. No stall may be on record: the boot above wrote
    # flash thousands of times, each inside a declared section.
    console.type_line("sleep 5; sysctl machdep.core1 machdep.core1_restarts "
                      "machdep.core1_faults machdep.core1_stalls")
    console.wait_lines(CORE1_LINES, "core 1 answering pings", COMMAND_BUDGET_SECONDS)
    # operator is in wheel, so su needs no password. The write masks core
    # 0's interrupts for 300 ms with no section declared; core 1 has to
    # count exactly that one stall, and the supervisor must not mistake
    # it for a dead core 1.
    console.type_line("su")
    console.wait_lines(["#"], "root shell", COMMAND_BUDGET_SECONDS)
    console.type_line("sysctl -w machdep.core1_stall_ms=300; "
                      "sysctl machdep.core1_stalls machdep.core1_restarts")
    console.wait_lines(STALL_LINES, "injected stall reported", COMMAND_BUDGET_SECONDS)


def main(argv: list[str]) -> int:
    if len(argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    renode_path, unix_bin, flash_bin, outdir = argv[:4]
    config = argv[4] if len(argv) == 5 else "PICO_UART"
    if config not in BOARD_LINES:
        print(f"check-renode: no probe lines for CONFIG {config}", file=sys.stderr)
        return 2
    try:
        identity = image_identity(unix_bin)
    except (OSError, ValueError, struct.error) as error:
        print(f"check-renode: {error}", file=sys.stderr)
        return 2
    os.makedirs(outdir, exist_ok=True)
    transcript = os.path.join(outdir, "uart0.txt")
    if os.path.exists(transcript):
        os.remove(transcript)
    renode = Renode(renode_path, os.path.join(outdir, "renode.log"), WALL_SECONDS)
    console = Console(renode, transcript)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        run_stages(console, config, identity)
    except StageFailed:
        # A kernel that panics stops at "press any key to reboot..."; the
        # whole transcript is in uart0.txt beside the log.
        print(f"check-renode: transcript in {transcript}", file=sys.stderr)
    except RenodeError as error:
        print(f"check-renode: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    if console.failures:
        return 1
    print("check-renode: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
