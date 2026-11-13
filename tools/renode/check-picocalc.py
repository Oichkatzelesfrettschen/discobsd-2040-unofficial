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

With CARD and FSUTIL the driver also loads Renode's SD card model with that
image and runs the crash-dump stages of picocalc_dump.py after the shell
stage: reboot -d, savecore, and a panic entered from the idle loop.

Usage: check-picocalc.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR [CARD FSUTIL]
       check-picocalc.py --self-test    calibrate the Status-line assertions
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import os
import re
import sys
from collections.abc import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import picocalc_dump  # noqa: E402
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


def panel_status_failures(status: str) -> list[str]:
    """The assertions a panel Status line fails, empty when it passes.

    The model renders graphics RAM whether or not the panel left sleep mode,
    so the rendered console alone cannot show that the driver sent SLPOUT;
    the line must read "sleep out" as well as "display on".
    """
    failures: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    expect(status.startswith("display on,"), "panel: DISPON was never received")
    expect(", sleep out," in status, "panel: SLPOUT was never received (still in sleep mode)")
    expect("madctl 0x48" in status, "panel: MADCTL 0x48 not programmed")
    expect("colmod 0x66" in status, "panel: COLMOD 0x66 not programmed")
    expect("scroll top 0 area 320 bottom 160" in status,
           "panel: VSCRDEF 0/320/160 not programmed")
    expect(" 0 unknown commands" in status, "panel: unmodeled commands received")
    expect(" 0 outside the window" in status, "panel: pixels written outside the window")
    expect(" 0 without chip select" in status, "panel: bytes arrived with chip select high")
    pixels = re.search(r"(\d+) pixels", status)
    count = int(pixels.group(1)) if pixels else 0
    expect(count > 10000, f"panel: {count} pixels written, expected a screenful")
    return failures


def check_panel_registers(run: Run) -> None:
    status = panel_status(run.renode)
    print(f"check-picocalc: panel {status}")
    for message in panel_status_failures(status):
        run.expect(False, message)


SPI1_SSPDR = 0x40040008
SPI1_SSPSR = 0x4004000C
SSPSR_TFE, SSPSR_TNF, SSPSR_RNE, SSPSR_BSY = 0x01, 0x02, 0x04, 0x10
FIFO_DEPTH = 8
FRAME_BYTE = 0xA5
SPI_SETTLE_SLICES = 40


def spi_fifo_failures(levels: dict[str, int], accepted: int, dropped: int,
                      sent: int) -> list[str]:
    """The assertions a PL022 transmit-FIFO observation fails.

    levels maps a label to the SSPSR value read after that many back-to-back
    SSPDR writes with virtual time held still: "one" after one frame, "fill"
    after eight, "full" after nine, "over" after the extra writes of a driver
    that skips the TNF wait, and "idle" once the wire has drained. The frame
    in the shift register is not in the FIFO, so nine writes fill an
    eight-entry FIFO; the writes beyond that are discarded, so the panel
    receives nine bytes however many were written. accepted and dropped are
    the panel's byte counter and without-chip-select counter deltas.
    """
    failures: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    one, fill = levels["one"], levels["fill"]
    full, over, idle = levels["full"], levels["over"], levels["idle"]
    expect(one & SSPSR_BSY != 0 and one & SSPSR_TFE != 0,
           "spi1: after one frame BSY must be set and the FIFO empty (the frame is shifting)")
    expect(fill & SSPSR_TNF != 0 and fill & SSPSR_TFE == 0,
           f"spi1: with {FIFO_DEPTH - 1} frames queued TNF must be set and TFE clear")
    expect(full & SSPSR_TNF == 0 and full & SSPSR_TFE == 0,
           f"spi1: with {FIFO_DEPTH} frames queued TNF must be clear")
    expect(over & SSPSR_TNF == 0, "spi1: TNF must stay clear while the FIFO is full")
    expect(idle & SSPSR_BSY == 0 and idle & SSPSR_TNF != 0 and idle & SSPSR_TFE != 0,
           "spi1: after the wire drains BSY must clear and TNF and TFE must set")
    expect(accepted == FIFO_DEPTH + 1,
           f"spi1: the panel received {accepted} bytes from {sent} writes; "
           f"expected {FIFO_DEPTH + 1} (a full FIFO discards the rest)")
    expect(dropped == accepted,
           f"spi1: {dropped} of {accepted} probe bytes reached the panel with chip select high")
    return failures


# SSPSR values of a controller that behaves as described above, and the
# observation of the model before the FIFO level existed (TNF and TFE
# constant, every write delivered).
GOOD_FIFO = ({"one": 0x17, "fill": 0x16, "full": 0x1c, "over": 0x1c, "idle": 0x0f},
             FIFO_DEPTH + 1, FIFO_DEPTH + 1, FIFO_DEPTH + 4)
UNBOUNDED_FIFO = ({"one": 0x17, "fill": 0x17, "full": 0x17, "over": 0x17, "idle": 0x0f},
                  FIFO_DEPTH + 4, FIFO_DEPTH + 4, FIFO_DEPTH + 4)


def panel_counter(renode: Renode, pattern: str) -> int:
    match = re.search(pattern, panel_status(renode))
    return int(match.group(1)) if match else -1


def spi_settle(renode: Renode) -> bool:
    """Run until the controller is idle: BSY clear and the FIFO empty.

    The console redraws after a keystroke or a timer tick, so the controller
    can still be busy when a stage ends; an idle kernel leaves it quiet.
    """
    for _ in range(SPI_SETTLE_SLICES):
        renode.command('emulation RunFor "0.05"')
        status = monitor_int(renode, f"sysbus ReadDoubleWord {SPI1_SSPSR:#x}")
        if status & SSPSR_BSY == 0 and status & SSPSR_TFE != 0:
            for _ in range(FIFO_DEPTH + 1):
                monitor_int(renode, f"sysbus ReadDoubleWord {SPI1_SSPDR:#x}")
            return True
    return False


def check_spi_fifo(run: Run) -> None:
    """Write past the transmit FIFO of the panel's controller and read SSPSR.

    The emulation is paused between monitor commands, so the frames queue at
    one virtual instant. They are bytes for a deselected panel; the Status
    counters show the controller did not hand it more than nine, and all of
    them would arrive if the model's TNF stayed set.
    """
    renode = run.renode
    if not spi_settle(renode):
        run.fail("spi1: the controller never went idle, so its FIFO cannot be probed")
        return
    bytes0 = panel_counter(renode, r"(\d+) bytes")
    dropped0 = panel_counter(renode, r"(\d+) without chip select")
    levels: dict[str, int] = {}
    sent = 0
    for label, count in (("one", 1), ("fill", FIFO_DEPTH), ("full", FIFO_DEPTH + 1),
                         ("over", FIFO_DEPTH + 4)):
        while sent < count:
            renode.command(f"sysbus WriteDoubleWord {SPI1_SSPDR:#x} {FRAME_BYTE:#x}")
            sent += 1
        levels[label] = monitor_int(renode, f"sysbus ReadDoubleWord {SPI1_SSPSR:#x}")
    accepted = panel_counter(renode, r"(\d+) bytes") - bytes0
    dropped = panel_counter(renode, r"(\d+) without chip select") - dropped0
    # Twelve frames last 7.7 microseconds at the driver's 12.5 MHz.
    renode.command('emulation RunFor "0.00002"')
    levels["idle"] = monitor_int(renode, f"sysbus ReadDoubleWord {SPI1_SSPSR:#x}")
    for _ in range(FIFO_DEPTH + 1):
        monitor_int(renode, f"sysbus ReadDoubleWord {SPI1_SSPDR:#x}")
    print("check-picocalc: spi1 SSPSR "
          + " ".join(f"{label} {value:#04x}" for label, value in levels.items())
          + f"; panel took {accepted} of {sent} writes")
    for message in spi_fifo_failures(levels, accepted, dropped, sent):
        run.fail(message)


# A Status line the model printed after a passing boot (st7365p.cs Status()).
GOOD_STATUS = (
    "display on, sleep out, invert on, madctl 0x48, colmod 0x66, "
    "scroll top 0 area 320 bottom 160 start 0, 779602 bytes, 250660 pixels, "
    "0 outside the window, 0 unknown commands, 0 without chip select")


def self_test() -> int:
    """Calibrate panel_status_failures on a good line and on mutants of it."""
    failures = panel_status_failures(GOOD_STATUS)
    if failures:
        print(f"self-test: the recorded good Status line failed: {failures}", file=sys.stderr)
        return 1
    mutants = {
        "sleeping panel": ("sleep out", "sleep in", "SLPOUT"),
        "display off": ("display on", "display off", "DISPON"),
        "wrong MADCTL": ("madctl 0x48", "madctl 0x00", "MADCTL"),
        "unknown command": ("0 unknown commands", "1 unknown commands", "unmodeled"),
        "no pixels": ("250660 pixels", "0 pixels", "pixels written"),
    }
    for name, (old, new, word) in mutants.items():
        broken = panel_status_failures(GOOD_STATUS.replace(old, new))
        if not any(word in message for message in broken):
            print(f"self-test: the {name} Status line was not rejected: {broken}",
                  file=sys.stderr)
            return 1
    levels, accepted, dropped, sent = GOOD_FIFO
    if spi_fifo_failures(levels, accepted, dropped, sent):
        print("self-test: the recorded good FIFO observation failed", file=sys.stderr)
        return 1
    levels, accepted, dropped, sent = UNBOUNDED_FIFO
    if len(spi_fifo_failures(levels, accepted, dropped, sent)) < 3:
        print("self-test: the unbounded FIFO observation was not rejected", file=sys.stderr)
        return 1
    print("check-picocalc self-test: ok (good line, sleeping panel and four other mutants, "
          "good and unbounded FIFO)")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) not in (4, 6):
        print(__doc__, file=sys.stderr)
        return 2
    renode_path, unix_bin, flash_bin, outdir = argv[:4]
    card, fsutil = (argv[4], argv[5]) if len(argv) == 6 else (None, None)
    os.makedirs(outdir, exist_ok=True)
    if card is not None:
        picocalc_dump.make_card(card, fsutil)
    renode = Renode(renode_path, os.path.join(outdir, "renode.log"), WALL_SECONDS)
    run = Run(renode, outdir)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'picocalc-machine.resc')}")
        if card is not None:
            picocalc_dump.attach_card(renode, os.path.abspath(card))
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
        check_spi_fifo(run)
        if card is not None:
            elf = os.path.splitext(unix_bin)[0] + ".elf"
            picocalc_dump.run_dump_stages(run, os.path.abspath(card), elf)
    except StageFailed:
        pass
    except RenodeError as error:
        print(f"check-picocalc: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    if card is not None:
        picocalc_dump.check_after_close(run, card, fsutil)
    if run.failures:
        return 1
    print("check-picocalc: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
