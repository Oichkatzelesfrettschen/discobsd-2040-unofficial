#!/usr/bin/env python3
"""Boot the PICOCALC kernel under Renode and assert on the panel and keyboard.

Renode runs with its monitor on stdin (no network listener), and this driver
advances virtual time in slices, dumps the panel model's frame after each,
decodes it to console text with picocalc_fb.py and waits for the lines each
stage expects. Virtual time is the budget, so a slow host lengthens the run
without failing it; WALL_SECONDS bounds a hung emulator.

Stages, each asserted on the decoded panel text:
  banner    the kernel banner, lcd0 and kbd0 probe lines, and the psram0
            probe falling back to swap on fl1 (the machine models no PSRAM)
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


# psram.c prints one outcome line per probe. The machine models no part on
# the PSRAM pins, so the probe must refuse it and leave swap on fl1: the path
# a PicoCalc whose PSRAM does not answer takes. The panel wraps a line at 53
# cells and drops trailing blanks, so the match runs on the screen text with
# every space removed.
PSRAM_FALLBACK = re.compile(
    r"psram0:(?:id[0-9a-f]+/[0-9a-f]+isnotanESP-PSRAM64H,"
    r"|addresseswrapbelow\d+kbytes,|notusable\(error-?\d+\),)swapstaysonfl1")
PSRAM_ATTACH = "psram0:ESP-PSRAM64H"
# fl1 is block major 0, minor 8; an attached PSRAM would report (3,0).
SWAP_ON_FL1 = "swap dev  = (0,8)"


def psram_squeezed(lines: list[str]) -> str:
    return "".join(line.replace(" ", "") for line in lines)


def psram_probe_seen(lines: list[str]) -> bool:
    squeezed = psram_squeezed(lines)
    return PSRAM_ATTACH in squeezed or PSRAM_FALLBACK.search(squeezed) is not None


def psram_failures(lines: list[str]) -> list[str]:
    """The psram0 outcome must be the fallback to fl1 and never an attach."""
    squeezed = psram_squeezed(lines)
    failures = []
    if PSRAM_ATTACH in squeezed:
        failures.append("psram0: the probe attached a PSRAM part the machine does not model")
    if PSRAM_FALLBACK.search(squeezed) is None:
        failures.append("psram0: no probe line leaving swap on fl1")
    return failures


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


LOGO_COLS, LOGO_ROWS = 14, 10
CELL_W, CELL_H = 6, 10


def logo_squares() -> set[tuple[int, int]]:
    """The mirror ball usr.bin/picofetch/picofetch.c computes, as 3 by 3
    pixel squares: (column, row) of each square it draws."""
    glint = {(0, 7), (27, 5), (0, 26), (27, 28)}
    squares = set()
    for y in range(LOGO_ROWS * 3):
        for x in range(LOGO_COLS * 2):
            dx, dy = 2 * x - 28, 2 * y - 33
            if dx * dx + dy * dy <= 625:
                if not (x % 4 == 2 or y % 3 == 0):
                    squares.add((x, y))
            elif (y < 4 and x == 14) or (x, y) in glint:
                squares.add((x, y))
    return squares


def logo_failures(width: int, pixels: bytes, top_row: int) -> list[str]:
    """Compare the panel pixels of the logo's cells with logo_squares().

    The logo's first cell row is console row top_row. A mosaic square is 3
    by 3 panel pixels; the tenth pixel line of a cell repeats the ninth, so
    the bottom squares of a cell are four lines tall. A square is lit when
    its center pixel is not black, and the facets must show more than one
    color, which a console that ignored SGR would not.
    """
    want = logo_squares()
    wrong = 0
    colors = set()
    for y in range(LOGO_ROWS * 3):
        for x in range(LOGO_COLS * 2):
            px = x * 3 + 1
            py = (top_row + y // 3) * CELL_H + (y % 3) * 3 + 1
            at = (py * width + px) * 3
            pixel = bytes(pixels[at:at + 3])
            lit = pixel != b"\0\0\0"
            if lit:
                colors.add(pixel)
            if lit != ((x, y) in want):
                wrong += 1
    failures = []
    if wrong:
        failures.append(f"picofetch: {wrong} of {LOGO_COLS * 2 * LOGO_ROWS * 3} logo "
                        "squares differ from the mirror ball")
    if len(colors) < 6:
        failures.append(f"picofetch: the logo shows {len(colors)} colors, expected the "
                        "six facet colors and more")
    return failures


def logo_frame(shift: int = 0, mono: bool = False) -> tuple[int, bytes]:
    """A 320-pixel-wide frame holding the logo at row 0, for the self-test."""
    width = 320
    pixels = bytearray(width * LOGO_ROWS * CELL_H * 3)
    palette = [(255, 0, 255), (92, 92, 255), (0, 255, 255), (0, 255, 0), (255, 255, 0),
               (255, 0, 0), (255, 255, 255)]
    for x, y in logo_squares():
        color = palette[0] if mono else palette[((x // 2 + 1) // 2 + y // 3) % 7]
        for line in range(4 if y % 3 == 2 else 3):
            for col in range(3):
                at = (((y // 3) * CELL_H + (y % 3) * 3 + line) * width
                      + (x + shift) * 3 + col) * 3
                pixels[at:at + 3] = bytes(color)
    return width, bytes(pixels)


def check_monitors(run: Run) -> None:
    """Run picofetch and picotop when the image carries them.

    picofetch must print its readings from this kernel's sysctl nodes beside
    the logo drawn from the glyph bank, and fill the capacity rows (Tables,
    Kstack) from kern.capacity, which PICOCALC serves; picotop must
    draw two frames and exit. An image without the programs (any profile but
    picocalc) skips the stage and says so.
    """
    # The keyboard model's FIFO holds 31 events, so a command longer than
    # fifteen keys goes in through picocalc_dump.Console, which types in
    # chunks and waits for each to drain.
    console = picocalc_dump.Console(run)
    console.type("ls /usr/bin/picofetch || echo fetch-none")
    run.wait_until(lambda: run.has_line("/usr/bin/picofetch") or run.has_line("fetch-none"),
                   "picofetch probe", "the path or fetch-none", KEY_BUDGET_SECONDS)
    if run.has_line("fetch-none"):
        print("check-picocalc: skip picofetch and picotop: the image carries neither "
              "(the picocalc profile does)")
        return
    run.type_line("picofetch")
    run.wait_for(["Reset  "], "picofetch report", KEY_BUDGET_SECONDS)
    run.wait_for_prompt("prompt after picofetch", KEY_BUDGET_SECONDS)
    run.snapshot("picofetch")
    for text in ("OS       DiscoBSD 2.7", "Kernel   PICOCALC", "CPU      125.000 MHz, ",
                 "SRAM     264K, process window 192K", "MPU      on, ",
                 "Display  318x320 px, 53x32 cells", "Tables   proc ", "Kstack   "):
        run.expect(run.has(text), f"picofetch: no {text!r} on the panel")
    run.expect(any(line.startswith("Kstack   ") and " peak " in line for line in run.lines),
               "picofetch: no Kstack peak from kern.capacity on the panel")
    for label in ("Memory  ", "Swap    ", "Root    ", "Battery ", "Flash    image "):
        run.expect(run.has(label), f"picofetch: no {label!r} line on the panel")
    # Renode's ADC is a model, so the line's form is checked and its values
    # are not.
    run.expect(any(re.match(r"Sensors  -?\d+\.\d C, VSYS \d+\.\d\d V", line)
                   for line in run.lines),
               "picofetch: no Sensors line with a temperature and VSYS on the panel")
    run.expect(not run.has("read failed"), "picofetch: a sysctl read failed")
    # The title row, not a prompt: a prompt carries the shell's $.
    title = [i for i, line in enumerate(run.lines)
             if "operator@" in line and "$" not in line]
    if not title:
        run.fail("picofetch: no operator@host title on the panel")
    else:
        width, _, pixels = picocalc_fb.read_ppm(os.path.join(run.outdir, "picofetch.ppm"))
        for message in logo_failures(width, pixels, title[-1]):
            run.fail(message)
    console.type("picotop -c 2 -d 1")
    run.wait_for(["q quit"], "picotop frame", KEY_BUDGET_SECONDS)
    run.wait_until(lambda: any(line.rstrip().endswith("$") for line in run.lines[-2:]),
                   "prompt after picotop", "a shell prompt on the last rows",
                   KEY_BUDGET_SECONDS)
    run.snapshot("picotop")
    for text in (" picotop  ", "CPU  ", "Mem  ", "Swp  ", "/    ", "Bat  ", "Evt  cs ",
                 "CPU%   TIME COMMAND"):
        run.expect(run.has(text), f"picotop: no {text!r} on the panel")
    run.expect(any(line.rstrip().endswith("picotop") and " R+ " in line for line in run.lines),
               "picotop: no running, in-core picotop row in the process table")


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
    width, pixels = logo_frame()
    if logo_failures(width, pixels, 0):
        print("self-test: the drawn mirror ball failed its own comparison", file=sys.stderr)
        return 1
    width, pixels = logo_frame(shift=1)
    if not any("squares differ" in m for m in logo_failures(width, pixels, 0)):
        print("self-test: a logo shifted one square was not rejected", file=sys.stderr)
        return 1
    width, pixels = logo_frame(mono=True)
    if not any("colors" in m for m in logo_failures(width, pixels, 0)):
        print("self-test: a one-color logo was not rejected", file=sys.stderr)
        return 1
    # The fallback line wrapped at the panel's 53 cells, as the console
    # draws it, must pass; an attach, a missing line and a fallback printed
    # by another driver must not.
    fallback = "psram0: id 0/0 is not an ESP-PSRAM64H, swap stays on fl1"
    wrapped = [fallback[:picocalc_fb.COLS], fallback[picocalc_fb.COLS:]]
    for name, lines in (("wrapped id refusal", ["kbd0: x"] + wrapped),
                        ("stall refusal", ["psram0: not usable (error 4), swap stays on fl1"])):
        if psram_failures(lines) or not psram_probe_seen(lines):
            print(f"self-test: the {name} psram0 line failed", file=sys.stderr)
            return 1
    for name, lines in (
            ("attach", ["psram0: ESP-PSRAM64H 8192 kbytes on PIO0, swap"]),
            ("missing", ["kbd0: keyboard controller at I2C1 0x1f, firmware 1.6"]),
            ("other driver", ["sd0: not usable (error 4), swap stays on fl1"])):
        if not psram_failures(lines):
            print(f"self-test: the {name} psram0 case was not rejected", file=sys.stderr)
            return 1
    print("check-picocalc self-test: ok (good line, sleeping panel and four other mutants, "
          "good and unbounded FIFO, good, shifted and one-color logo, psram0 fallback "
          "and three mutants)")
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
        run.wait_until(lambda: psram_probe_seen(run.lines), "psram probe",
                       "a psram0 outcome line", BOOT_BUDGET_SECONDS)
        run.snapshot("banner")
        for message in psram_failures(run.lines):
            run.fail(message)
        run.wait_for([SWAP_ON_FL1], "swap device after the psram probe", BOOT_BUDGET_SECONDS)
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
        check_monitors(run)
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
