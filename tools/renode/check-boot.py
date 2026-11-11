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
    shell that cannot execute anything: uname, the capacity record, the
    MPU fault test and the exec residue probe.

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
import sys

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
    "Starting daemons:  update",
]

# The formatter accepts the versioned kernel structure only when its compiled
# size agrees. These stable fragments pin every fixed-table limit and the
# u-area geometry without treating one emulator workload's dynamic peaks as
# board headroom.
CAPACITY_LINES = [
    "kern.capacity=version 1 size 100",
    "proc limit 25 current",
    "inode limit 24 current",
    "file limit 24 current",
    "clist limit 32 current",
    "buffer limit 4 current",
    "uarea bytes 3072 user 972 stack 2048 peak",
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


def run_stages(console: Console, config: str) -> None:
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
    console.type_line("sysctl kern.capacity")
    console.wait_lines(CAPACITY_LINES, "capacity record", COMMAND_BUDGET_SECONDS)
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
        run_stages(console, config)
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
