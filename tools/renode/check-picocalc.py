#!/usr/bin/env python3
"""Boot the PICOCALC kernel under Renode and assert on the panel and keyboard.

Renode runs with its monitor on stdin (no network listener), and this driver
advances virtual time in slices, dumps the panel model's frame after each,
decodes it to console text with picocalc_fb.py and waits for the lines each
stage expects. Virtual time is the budget, so a slow host lengthens the run
without failing it; WALL_SECONDS bounds a hung emulator.

Stages, each asserted on the decoded panel text:
  banner    the kernel banner, lcd0 and kbd0 probe lines
  login     the getty prompt, after the root check, daemons and init
  shell     keys typed through the I2C keyboard model log in and run uname

The panel model's counters and registers are asserted from its Status line:
the init commands the driver sends (MADCTL, COLMOD, VSCRDEF, DISPON), no
unmodeled commands, and no pixel written outside the CASET/RASET window.

Usage: check-picocalc.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import os
import re
import sys
from collections.abc import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import picocalc_fb  # noqa: E402
from renode_monitor import Renode, RenodeError, monitor_int  # noqa: E402

SLICE_SECONDS = 0.5
BOOT_BUDGET_SECONDS = 20.0
LOGIN_BUDGET_SECONDS = 120.0
KEY_BUDGET_SECONDS = 20.0
WALL_SECONDS = 3300.0

BANNER_LINES = [
    "DiscoBSD 2.7 (PICOCALC)",
    "lcd0: ST7365P 320x320 on SPI1, 53x32 cells, console",
    "kbd0: keyboard controller at I2C1 0x1f, firmware 1.6",
]


class StageFailed(Exception):
    """A stage's expected text never reached the panel; later stages cannot run."""


class Run:
    def __init__(self, renode: Renode, outdir: str) -> None:
        self.renode = renode
        self.outdir = outdir
        self.virtual = 0.0
        self.frame = 0
        self.lines: list[str] = []
        self.failures: list[str] = []

    def advance(self, seconds: float = SLICE_SECONDS) -> list[str]:
        self.renode.command(f'emulation RunFor "{seconds}"')
        self.virtual += seconds
        path = os.path.join(self.outdir, "frame.ppm")
        self.renode.command(f"sysbus.spi1.lcd DumpPpm @{path}")
        self.lines = picocalc_fb.screen_text(path).split("\n")
        if os.environ.get("CHECK_PICOCALC_VERBOSE"):
            last = [line for line in self.lines if line.strip()][-1:] or [""]
            print(f"check-picocalc: t={self.virtual:.1f}s {last[0]!r}", flush=True)
        return self.lines

    def snapshot(self, name: str) -> None:
        path = os.path.join(self.outdir, name)
        self.renode.command(f"sysbus.spi1.lcd DumpPpm @{path}.ppm")
        width, height, pixels = picocalc_fb.read_ppm(path + ".ppm")
        picocalc_fb.write_png(path + ".png", width, height, pixels)
        with open(path + ".txt", "w", encoding="utf-8") as handle:
            handle.write("\n".join(self.lines) + "\n")

    def has(self, text: str) -> bool:
        return any(text in line for line in self.lines)

    def has_line(self, text: str) -> bool:
        return any(line.strip() == text for line in self.lines)

    def type_line(self, text: str, erase_last: bool = False) -> None:
        """Queue text and Enter on the keyboard model.

        With erase_last the final character goes in as a wrong one and is
        removed with Backspace first, which exercises the controller's
        Backspace key, the driver's DEL mapping and the tty's erase echo.
        """
        kbd = "sysbus.i2c1.kbd"
        if erase_last:
            self.renode.command(f'{kbd} TypeText "{text[:-1]}x"')
            self.renode.command(f"{kbd} TypeKey 8")
            self.renode.command(f'{kbd} TypeText "{text[-1]}"')
            self.renode.command(f"{kbd} TypeKey 10")
        else:
            self.renode.command(f'{kbd} TypeLine "{text}"')

    def wait_until(self, predicate: Callable[[], bool], what: str, expect: str,
                   budget: float) -> bool:
        spent = 0.0
        while spent < budget:
            self.advance()
            spent += SLICE_SECONDS
            if predicate():
                print(f"check-picocalc: {what} after {self.virtual:.1f} virtual seconds")
                return True
        self.fail(f"{what}: {expect} not on the panel after {self.virtual:.1f} "
                  "virtual seconds")
        raise StageFailed(what)

    def wait_for(self, texts: list[str], what: str, budget: float) -> bool:
        return self.wait_until(lambda: all(self.has(text) for text in texts), what,
                               repr(texts), budget)

    def wait_for_line(self, text: str, what: str, budget: float) -> bool:
        return self.wait_until(lambda: self.has_line(text), what, f"a line {text!r}", budget)

    def wait_for_prompt(self, what: str, budget: float) -> bool:
        """Wait for a line that ends in the shell's $ and has no other text after it."""
        return self.wait_until(
            lambda: any(line.rstrip().endswith("$") for line in self.lines),
            what, "a shell prompt", budget)

    def fail(self, message: str) -> None:
        print(f"check-picocalc: FAIL {message}", file=sys.stderr)
        self.failures.append(message)

    def expect(self, condition: bool, message: str) -> None:
        if not condition:
            self.fail(message)


DRIVER_SOURCE = os.path.normpath(
    os.path.join(HERE, "..", "..", "sys", "arch", "rp2040", "dev", "st7365p.c"))


def expected_init_trace(path: str = DRIVER_SOURCE) -> list[str]:
    """The panel bytes st7365p_init() sends before SLPOUT, from its source.

    Each init_seq entry is a command byte, a parameter count and the
    parameters; the command goes out with D/C low (token cNN) and each
    parameter with D/C high (dNN). Macro names resolve through the
    #define lines of the same file.
    """
    with open(path, encoding="ascii") as handle:
        text = handle.read()
    macros = dict(re.findall(r"#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\b", text))
    body = re.search(r"init_seq\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if body is None:
        raise ValueError(f"{path}: no init_seq")
    values = []
    for token in re.findall(r"[A-Za-z_]\w*|0x[0-9a-fA-F]+|\d+",
                            re.sub(r"/\*.*?\*/", "", body.group(1), flags=re.S)):
        values.append(int(macros.get(token, token), 0))
    trace: list[str] = []
    index = 0
    while index < len(values):
        command, count = values[index], values[index + 1]
        trace.append(f"c{command:02x}")
        trace.extend(f"d{value:02x}" for value in values[index + 2:index + 2 + count])
        index += 2 + count
    return trace


def panel_status(renode: Renode) -> str:
    out = renode.command("sysbus.spi1.lcd Status")
    for line in out.splitlines():
        if line.startswith("display "):
            return line
    return ""


def check_panel_registers(run: Run) -> None:
    status = panel_status(run.renode)
    print(f"check-picocalc: panel {status}")
    run.expect(", display on," in status or status.startswith("display on"),
               "panel: DISPON was never received")
    run.expect("madctl 0x48" in status, "panel: MADCTL 0x48 not programmed")
    run.expect("colmod 0x66" in status, "panel: COLMOD 0x66 not programmed")
    run.expect("scroll top 0 area 320 bottom 160" in status,
               "panel: VSCRDEF 0/320/160 not programmed")
    run.expect(" 0 unknown commands" in status, "panel: unmodeled commands received")
    run.expect(" 0 outside the window" in status, "panel: pixels written outside the window")
    run.expect(" 0 without chip select" in status, "panel: bytes arrived with chip select high")
    pixels = re.search(r"(\d+) pixels", status)
    count = int(pixels.group(1)) if pixels else 0
    run.expect(count > 10000, f"panel: {count} pixels written, expected a screenful")


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    renode_path, unix_bin, flash_bin, outdir = argv
    os.makedirs(outdir, exist_ok=True)
    renode = Renode(renode_path, os.path.join(outdir, "renode.log"), WALL_SECONDS)
    run = Run(renode, outdir)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'picocalc-machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{os.path.join(outdir, 'uart0.txt')} true")
        init_trace = expected_init_trace()
        renode.command(f"sysbus.spi1.lcd TraceBytes {len(init_trace)}")
        run.wait_for(BANNER_LINES, "banner", BOOT_BUDGET_SECONDS)
        run.snapshot("banner")
        check_panel_registers(run)
        # The monitor echoes prompts and command lines around its output.
        traced = [line for line in renode.command("sysbus.spi1.lcd Trace").splitlines()
                  if re.fullmatch(r"\s*([cd][0-9a-f]{2}\s*)+", line)]
        seen = traced[-1].split() if traced else []
        run.expect(seen == init_trace,
                   f"panel: init bytes differ from st7365p.c's init_seq: {seen[:12]}")
        run.wait_for(["login:"], "login prompt", LOGIN_BUDGET_SECONDS)
        run.snapshot("login")
        # The operator account has an empty password (the image checker
        # verifies it), so the name alone reaches a shell.
        run.type_line("operator")
        run.wait_for_prompt("shell prompt", KEY_BUDGET_SECONDS)
        run.type_line("uname -sr", erase_last=True)
        run.wait_for_line("DiscoBSD 2.7", "uname answer typed through the keyboard model",
                          KEY_BUDGET_SECONDS)
        run.snapshot("shell")
        # Nine keys to log in and twelve for the command (the wrong character and its
        # Backspace included), a press and a release each.
        queued = monitor_int(renode, "sysbus.i2c1.kbd Queued")
        delivered = monitor_int(renode, "sysbus.i2c1.kbd Delivered")
        dropped = monitor_int(renode, "sysbus.i2c1.kbd Dropped")
        run.expect(queued == 42 and delivered == 42 and dropped == 0,
                   f"keyboard: {queued} events queued, {delivered} delivered through "
                   f"register 0x09, {dropped} dropped; expected 42, 42 and 0")
        transactions = monitor_int(renode, "sysbus.i2c1.kbd Transactions")
        run.expect(transactions > 0, "keyboard: no I2C transaction reached 0x1f")
    except StageFailed:
        pass
    except RenodeError as error:
        print(f"check-picocalc: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    if run.failures:
        return 1
    print("check-picocalc: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
