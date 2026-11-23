#!/usr/bin/env python3
"""Boot the PICOCALC kernel under Renode and assert on the panel and keyboard.

Renode runs with its monitor on stdin (no network listener), and this driver
advances virtual time in slices, dumps the panel model's frame after each,
decodes it to console text with picocalc_fb.py and waits for the lines each
stage expects. Virtual time is the budget, so a slow host lengthens the run
without failing it; WALL_SECONDS bounds a hung emulator.

Stages, each asserted on the decoded panel text:
  banner    the kernel banner, lcd0 and kbd0 probe lines, and the psram0
            probe falling back to swap on fl1 (the machine models no PSRAM);
            the banner wait counts every frame, taken 0.1 virtual second apart
  login     the getty prompt, after the root check, daemons and init
  shell     keys typed through the I2C keyboard model log in and run uname

Before the kernel runs, the panel model answers the ST7365P read commands
(RDDID, RDDST, RDDPM, RDDMADCTL, RDDCOLMOD, RDDIM, RDDSM, RDDSDR, RDID1 to
RDID3) with the values ST7365P_SPEC_V1.0 gives after a reset and after the
driver's init sequence, and holds them across a software reset.

The panel model's counters and registers are asserted from its Status line:
the init commands the driver sends (MADCTL, COLMOD, VSCRDEF, DISPON), no
unmodeled commands, and no pixel written outside the CASET/RASET window.

With CARD and FSUTIL the driver also loads Renode's SD card model with that
image and runs the crash-dump stages of picocalc_dump.py after the shell
stage: reboot -d, savecore, and a panic entered from the idle loop.

Usage: check-picocalc.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR [CARD FSUTIL]
       check-picocalc.py --readback RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR
                                        only the panel read commands, no boot
       check-picocalc.py --self-test    calibrate the Status-line assertions
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import collections
import contextlib
import io
import os
import re
import sys
import tempfile
import typing
from collections.abc import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import picocalc_dump  # noqa: E402
import picocalc_fb  # noqa: E402
from renode_monitor import Renode, RenodeError, monitor_int  # noqa: E402

SLICE_SECONDS = 0.5
# A wait prints a line every this many virtual seconds: a hosted runner emulates
# a few virtual seconds per minute, and the gate runner ends a step whose output
# is silent for its idle timeout.
PROGRESS_SECONDS = 10.0
# A 32-row panel holds a boot line for a fraction of a second once the console
# scrolls: the banner of the diagnostic kernel is on the panel for 0.2 to 0.3
# virtual seconds. A wait for such a line takes frames this far apart and counts
# every frame (Run.wait_seen), because a frame every SLICE_SECONDS samples a
# window that short once at most and misses it when the boot text arrives a
# tenth of a virtual second later.
EARLY_SLICE_SECONDS = 0.1
# A failed run writes the first frames and the last ones to frames.txt: the
# head covers the boot text the early waits look for, the tail the screen the
# run ended on.
FRAMES_KEPT_HEAD = 60
FRAMES_KEPT_TAIL = 8
BOOT_BUDGET_SECONDS = 20.0
LOGIN_BUDGET_SECONDS = 120.0
KEY_BUDGET_SECONDS = 20.0
WALL_SECONDS = 3300.0

BANNER_LINES = [
    "GizmoBSD 0.1.0-alpha (PICOCALC)",
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
    r"|addresseswrapbelow\d+kbytes,|notusable\(error-?\d+\),)swapstaysonfl1"
)
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
        # Every line a frame has shown; boot text scrolls off a 32-row panel.
        self.seen: set[str] = set()
        self.failures: list[str] = []
        # The first and the last frames, written to frames.txt by fail().
        self.frames_taken = 0
        self.frames_head: list[tuple[int, float, list[str]]] = []
        self.frames_tail: collections.deque[tuple[int, float, list[str]]] = collections.deque(
            maxlen=FRAMES_KEPT_TAIL
        )

    def advance(self, seconds: float = SLICE_SECONDS) -> list[str]:
        self.renode.command(f'emulation RunFor "{seconds}"')
        self.virtual += seconds
        path = os.path.join(self.outdir, "frame.ppm")
        self.renode.command(f"sysbus.spi1.lcd DumpPpm @{path}")
        return self.show(picocalc_fb.screen_text(path).split("\n"))

    def show(self, lines: list[str]) -> list[str]:
        """Take the panel text of the frame at the current virtual time."""
        self.lines = lines
        self.seen.update(line.rstrip() for line in lines)
        self.frames_taken += 1
        frame = (self.frames_taken, self.virtual, [line.rstrip() for line in lines if line.strip()])
        if len(self.frames_head) < FRAMES_KEPT_HEAD:
            self.frames_head.append(frame)
        self.frames_tail.append(frame)
        if os.environ.get("CHECK_PICOCALC_VERBOSE"):
            last = [line for line in lines if line.strip()][-1:] or [""]
            print(f"check-picocalc: t={self.virtual:.1f}s {last[0]!r}", flush=True)
        return lines

    def write_frames(self) -> str:
        """Write the kept frames as text, so a failed wait shows what the panel held."""
        path = os.path.join(self.outdir, "frames.txt")
        shown = {frame[0] for frame in self.frames_head}
        tail = [frame for frame in self.frames_tail if frame[0] not in shown]
        with open(path, "w", encoding="utf-8") as handle:
            for number, virtual, text in self.frames_head:
                handle.write(f"frame {number} t={virtual:.1f}s\n")
                handle.writelines(f"  {line}\n" for line in text or ["(blank)"])
            if tail and tail[0][0] > len(self.frames_head) + 1:
                handle.write(
                    f"frames {len(self.frames_head) + 1} to {tail[0][0] - 1} are not kept\n"
                )
            for number, virtual, text in tail:
                handle.write(f"frame {number} t={virtual:.1f}s\n")
                handle.writelines(f"  {line}\n" for line in text or ["(blank)"])
        return path

    def snapshot(self, name: str) -> None:
        path = os.path.join(self.outdir, name)
        self.renode.command(f"sysbus.spi1.lcd DumpPpm @{path}.ppm")
        width, height, pixels = picocalc_fb.read_ppm(path + ".ppm")
        picocalc_fb.write_png(path + ".png", width, height, pixels)
        with open(path + ".txt", "w", encoding="utf-8") as handle:
            handle.write("\n".join(self.lines) + "\n")

    def has(self, text: str) -> bool:
        return any(text in line for line in self.lines)

    def saw(self, text: str) -> bool:
        """Whether any frame since the run began showed text."""
        return any(text in line for line in self.seen)

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

    def wait_until(
        self,
        predicate: Callable[[], bool],
        what: str,
        expect: str,
        budget: float,
        slice_seconds: float = SLICE_SECONDS,
    ) -> bool:
        spent = 0.0
        noted = 0.0
        while spent < budget:
            self.advance(slice_seconds)
            spent += slice_seconds
            if predicate():
                print(f"check-picocalc: {what} after {self.virtual:.1f} virtual seconds")
                return True
            if spent - noted >= PROGRESS_SECONDS:
                noted = spent
                print(
                    f"check-picocalc: waiting for {what}: {spent:.0f} of {budget:.0f} "
                    f"virtual seconds spent",
                    flush=True,
                )
        self.fail(f"{what}: {expect} not on the panel after {self.virtual:.1f} virtual seconds")
        raise StageFailed(what)

    def wait_for(self, texts: list[str], what: str, budget: float) -> bool:
        return self.wait_until(
            lambda: all(self.has(text) for text in texts), what, repr(texts), budget
        )

    def wait_seen(self, texts: list[str], what: str, budget: float) -> bool:
        """Wait until every text has been on the panel in some frame.

        The texts need not share a frame, and the frames are EARLY_SLICE_SECONDS
        apart: the lines this waits for scroll off a 32-row panel within a
        fraction of a second, which a wait on the current frame at SLICE_SECONDS
        steps (wait_for) can sample once or not at all."""
        return self.wait_until(
            lambda: all(self.saw(text) for text in texts),
            what,
            repr(texts),
            budget,
            EARLY_SLICE_SECONDS,
        )

    def wait_for_line(self, text: str, what: str, budget: float) -> bool:
        return self.wait_until(lambda: self.has_line(text), what, f"a line {text!r}", budget)

    def wait_for_prompt(self, what: str, budget: float) -> bool:
        """Wait for a line that ends in the shell's $ and has no other text after it."""
        return self.wait_until(
            lambda: any(line.rstrip().endswith("$") for line in self.lines),
            what,
            "a shell prompt",
            budget,
        )

    def fail(self, message: str) -> None:
        print(f"check-picocalc: FAIL {message}", file=sys.stderr)
        self.failures.append(message)
        if self.outdir:
            print(f"check-picocalc: frames kept in {self.write_frames()}", file=sys.stderr)

    def expect(self, condition: bool, message: str) -> None:
        if not condition:
            self.fail(message)


DRIVER_SOURCE = os.path.normpath(
    os.path.join(HERE, "..", "..", "sys", "arch", "rp2040", "dev", "st7365p.c")
)


def expected_init_trace(path: str = DRIVER_SOURCE) -> list[str]:
    """The panel bytes st7365p_init() sends before SLPOUT, from its source.

    st7365p_init() sends init_unlock, init_seq and init_frame in that
    order. Each entry of a table is a command byte, a parameter count and
    the parameters; the command goes out with D/C low (token cNN) and each
    parameter with D/C high (dNN). Macro names resolve through the #define
    lines of the same file. init_frame comes from FRAME_SET(init_frame,
    FRS, DIVA, RTNA, Hz): BPC B5h 02h 02h 00h 04h, then FRMCTR1 B1h with
    FRS << 4 | DIVA and RTNA.
    """
    with open(path, encoding="ascii") as handle:
        text = handle.read()
    macros = dict(re.findall(r"#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\b", text))
    values = []
    for name in ("init_unlock", "init_seq"):
        body = re.search(name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
        if body is None:
            raise ValueError(f"{path}: no {name}")
        for token in re.findall(
            r"[A-Za-z_]\w*|0x[0-9a-fA-F]+|\d+", re.sub(r"/\*.*?\*/", "", body.group(1), flags=re.S)
        ):
            values.append(int(macros.get(token, token), 0))
    frame = re.search(r"FRAME_SET\(init_frame,\s*(\d+),\s*(\d+),\s*(\d+),", text)
    if frame is None:
        raise ValueError(f"{path}: no FRAME_SET(init_frame, ...)")
    frs, diva, rtna = (int(field) for field in frame.groups())
    values += [0xB5, 4, 0x02, 0x02, 0x00, 0x04, 0xB1, 2, frs << 4 | diva, rtna]
    trace: list[str] = []
    index = 0
    while index < len(values):
        command, count = values[index], values[index + 1]
        trace.append(f"c{command:02x}")
        trace.extend(f"d{value:02x}" for value in values[index + 2 : index + 2 + count])
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
    expect("colmod 0x55" in status, "panel: COLMOD 0x55 not programmed")
    expect("scroll top 0 area 320 bottom 160" in status, "panel: VSCRDEF 0/320/160 not programmed")
    expect(" 0 unknown commands" in status, "panel: unmodeled commands received")
    expect(" 0 outside the window" in status, "panel: pixels written outside the window")
    expect(" 0 without chip select" in status, "panel: bytes arrived with chip select high")
    pixels = re.search(r"(\d+) pixels", status)
    count = int(pixels.group(1)) if pixels else 0
    expect(count > 10000, f"panel: {count} pixels written, expected a screenful")
    return failures


def decode_read(cmd: int, raw: list[int]) -> int:
    """The register value in the bytes a host clocked in after a read command.

    The panel shifts the data on the clocks that follow the command byte,
    most significant bit first, after the dummy cycle RDDID and RDDST carry.
    """
    _, dummy, bits = READ_COMMANDS[cmd]
    stream = "".join(f"{byte:08b}" for byte in raw)
    return int(stream[dummy : dummy + bits], 2)


def read_failures(state: str, values: dict[int, int], want: dict[int, int]) -> list[str]:
    """The registers of values that differ from want, under READ_MASKS.

    READ_STATE_MASKS adds the bits a register leaves unspecified in one state.
    """
    if state not in (STATE_RESET, STATE_INIT, STATE_SWRESET):
        raise ValueError(f"unknown readback state {state!r}")
    failures = []
    for cmd, (name, _, _) in READ_COMMANDS.items():
        mask = READ_MASKS.get(cmd, 0xFFFFFFFF)
        mask &= READ_STATE_MASKS.get(state, {}).get(cmd, 0xFFFFFFFF)
        got = values.get(cmd, -1)
        if got < 0 or got & mask != want[cmd] & mask:
            failures.append(
                f"panel read {state}: {name} (0x{cmd:02x}) returned "
                f"{got:#x}, expected {want[cmd]:#x}"
            )
    return failures


LCD = "sysbus.spi1.lcd"


def panel_send(renode: Renode, tokens: list[str]) -> None:
    """Clock cNN (command) and dNN (data) bytes into the panel with chip select low.

    The panel's chip select and D/C arrive as GPIO inputs 0 and 1; the model's
    Transmit() is what the PL022 calls with each frame (patch 0008).
    """
    renode.command(f"{LCD} OnGPIO 0 false")
    level = None
    for token in tokens:
        data = token[0] == "d"
        if data != level:
            renode.command(f"{LCD} OnGPIO 1 {'true' if data else 'false'}")
            level = data
        renode.command(f"{LCD} Transmit {int(token[1:], 16):#x}")
    renode.command(f"{LCD} OnGPIO 1 true")
    renode.command(f"{LCD} OnGPIO 0 true")


def panel_read(renode: Renode, cmd: int) -> int:
    """One read command: the command byte, then dummy frames for the reply."""
    _, dummy, bits = READ_COMMANDS[cmd]
    frames = (dummy + bits + 7) // 8
    renode.command(f"{LCD} OnGPIO 0 false")
    renode.command(f"{LCD} OnGPIO 1 false")
    monitor_int(renode, f"{LCD} Transmit {cmd:#x}")
    renode.command(f"{LCD} OnGPIO 1 true")
    raw = [monitor_int(renode, f"{LCD} Transmit 0x00") for _ in range(frames)]
    renode.command(f"{LCD} OnGPIO 0 true")
    if any(byte < 0 or byte > 255 for byte in raw):
        return -1
    return decode_read(cmd, raw)


def check_panel_readback(run: Run) -> None:
    """Read every ST7365P read command at power-on, after the driver's bytes
    and after SWRESET, with the emulated CPU not yet started.

    The bytes are st7365p.c's startup tables (parsed from the source) and the calls
    that follow it, so the values read are what the board's driver leaves in
    the controller. The read commands must also stay out of the model's
    unmodeled-command count. The model returns to its power-on state before
    the boot stage starts.
    """
    renode = run.renode
    renode.command(f"{LCD} Reset")
    unknown0 = panel_counter(renode, r"(\d+) unknown commands")
    stages = (
        (STATE_RESET, [], READ_AFTER_RESET),
        (STATE_INIT, expected_init_trace() + INIT_TAIL, READ_AFTER_INIT),
        (STATE_SWRESET, ["c01"], READ_AFTER_SWRESET),
    )
    for state, tokens, want in stages:
        if tokens:
            panel_send(renode, tokens)
        values = {cmd: panel_read(renode, cmd) for cmd in READ_COMMANDS}
        print(
            "check-picocalc: panel read "
            + state
            + ": "
            + " ".join(f"{READ_COMMANDS[cmd][0]} {value:#x}" for cmd, value in values.items())
        )
        for message in read_failures(state, values, want):
            run.fail(message)
    unknown = panel_counter(renode, r"(\d+) unknown commands") - unknown0
    run.expect(unknown == 0, f"panel read: {unknown} read or init commands counted as unmodeled")
    renode.command(f"{LCD} Reset")


# Six RGB565 pixels written at COLMOD 55h into a 3 by 2 window, and the
# replies RAMRD (2Eh) gives for them: under 55h one dummy byte and the values
# high byte first; past the window's last pixel the read wraps to its first;
# under 66h three bytes a pixel with red and blue narrowed to five bits, as
# the board stores a 565 pixel (PICOCALC.md, kernels 4399 and 4401: white
# reads f8 fc f8). A pixel written at 66h reads back as written.
MEMORY_PIXELS = [0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0x8410]
MEMORY_REPLY_565 = [0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF, 0x00, 0x00, 0x84, 0x10]
MEMORY_REPLY_666 = [
    0xF8,
    0x00,
    0x00,
    0x00,
    0xFC,
    0x00,
    0x00,
    0x00,
    0xF8,
    0xF8,
    0xFC,
    0xF8,
    0x00,
    0x00,
    0x00,
    0x80,
    0x80,
    0x80,
]
MEMORY_WINDOW = ["c2a", "d00", "d00", "d00", "d02", "c2b", "d00", "d00", "d00", "d01"]


def memory_read(renode: Renode, frames: int) -> list[int]:
    """RAMRD over MEMORY_WINDOW: the bytes of frames reply frames, dummy first."""
    panel_send(renode, MEMORY_WINDOW)
    renode.command(f"{LCD} OnGPIO 0 false")
    renode.command(f"{LCD} OnGPIO 1 false")
    monitor_int(renode, f"{LCD} Transmit 0x2e")
    renode.command(f"{LCD} OnGPIO 1 true")
    raw = [monitor_int(renode, f"{LCD} Transmit 0x00") for _ in range(frames)]
    renode.command(f"{LCD} OnGPIO 0 true")
    return raw


def memory_failures(r565: list[int], wrap: list[int], r666: list[int], own: list[int]) -> list[str]:
    """The ways the four memory reads of check_panel_memory() go wrong."""
    failures = []
    if r565[1:] != MEMORY_REPLY_565:
        failures.append(f"panel RAMRD 55h: {r565[1:]}, expected {MEMORY_REPLY_565}")
    if wrap[-2:] != MEMORY_REPLY_565[:2]:
        failures.append(f"panel RAMRD past the window: {wrap[-2:]}, expected the first pixel")
    if r666[1:] != MEMORY_REPLY_666:
        failures.append(f"panel RAMRD 66h of 565 pixels: {r666[1:]}, expected {MEMORY_REPLY_666}")
    if own[1:4] != [0x84, 0x80, 0x84]:
        failures.append(f"panel RAMRD 66h of a 66h pixel: {own[1:4]}, expected [132, 128, 132]")
    return failures


def check_panel_memory(run: Run) -> None:
    """Write known pixels and read them back with RAMRD, before the CPU starts.

    The model answers RAMRD from its graphics RAM, so the kernel's LCDG_READ,
    scrshot and uLisp read-pixel run under Renode; a model that answers zeros,
    drops the dummy byte, loses the window wrap or the 565 narrowing fails one
    of the four reads. The model returns to its power-on state afterwards.
    """
    renode = run.renode
    renode.command(f"{LCD} Reset")
    pixels = [f"d{b:02x}" for v in MEMORY_PIXELS for b in (v >> 8, v & 0xFF)]
    panel_send(renode, ["c3a", "d55"] + MEMORY_WINDOW + ["c2c"] + pixels)
    r565 = memory_read(renode, 1 + 12)
    wrap = memory_read(renode, 1 + 14)
    panel_send(renode, ["c3a", "d66"])
    r666 = memory_read(renode, 1 + 18)
    panel_send(renode, MEMORY_WINDOW + ["c2c", "d84", "d80", "d84"])
    own = memory_read(renode, 1 + 3)
    print("check-picocalc: panel memory read: " + " ".join(f"{b:02x}" for b in r565[1:]))
    for message in memory_failures(r565, wrap, r666, own):
        run.fail(message)
    renode.command(f"{LCD} Reset")


def check_panel_registers(run: Run) -> None:
    status = panel_status(run.renode)
    print(f"check-picocalc: panel {status}")
    for message in panel_status_failures(status):
        run.expect(False, message)


# The ST7365P read commands (ST7365P_SPEC_V1.0 sections 9.2.3, 9.2.5 to 9.2.11
# and 9.2.47 to 9.2.49): name, dummy clock cycles before the first data bit,
# data bits. Page 95 draws the dummy cycle for RDDID (24-bit read) and RDDST
# (32-bit read) and none for the 8-bit reads.
READ_COMMANDS = {
    0x04: ("RDDID", 1, 24),
    0x09: ("RDDST", 1, 32),
    0x0A: ("RDDPM", 0, 8),
    0x0B: ("RDDMADCTL", 0, 8),
    0x0C: ("RDDCOLMOD", 0, 8),
    0x0D: ("RDDIM", 0, 8),
    0x0E: ("RDDSM", 0, 8),
    0x0F: ("RDDSDR", 0, 8),
    0xDA: ("RDID1", 0, 8),
    0xDB: ("RDID2", 0, 8),
    0xDC: ("RDID3", 0, 8),
}

# What each register reads, from the datasheet's Default tables (pages 147,
# 151, 153, 155, 157, 159, 160, 162, 215 to 217) and from the field layouts
# for the driver's configuration: MADCTL 48h (MX, BGR), COLMOD 55h, INVON,
# SLPOUT and DISPON. RDDST after the driver: BSTON 1, MX 1, RGB 1 (A4h);
# IFPF 101, SLPOUT 1, NORON 1 (53h); INVON 1, DISON 1 (24h); gamma curve 0.
# Masks hold the bits the model leaves at zero because the datasheet does not
# say when they set: ST15 of RDDST and VSSON of RDDIM (vertical scroll mode).
READ_MASKS = {0x09: 0xFFFF7FFF, 0x0D: 0x7F}
# RDDSDR reads 00h after a power-on, hardware or software reset (page 162).
# Page 162 describes the register as the result of the self-diagnostic that
# SLPOUT runs (page 165) and gives D7 (register loading detection) and D6
# (functionality detection) no polarity for a passed diagnostic, so after the
# driver's init sequence the check masks them; D5 to D1 stay 0 and D0 is the
# checksum comparison.
STATE_RESET = "after reset"
STATE_INIT = "after the driver's init"
STATE_SWRESET = "after SWRESET"
READ_STATE_MASKS = {STATE_INIT: {0x0F: 0x3F}}
READ_AFTER_RESET = {
    0x04: 0xFFFFFF,
    0x09: 0x00610000,
    0x0A: 0x08,
    0x0B: 0x00,
    0x0C: 0x06,
    0x0D: 0x00,
    0x0E: 0x00,
    0x0F: 0x00,
    0xDA: 0xFF,
    0xDB: 0xFF,
    0xDC: 0xFF,
}
READ_AFTER_INIT = {
    0x04: 0xFFFFFF,
    0x09: 0xA4532400,
    0x0A: 0x9C,
    0x0B: 0x48,
    0x0C: 0x55,
    0x0D: 0x20,
    0x0E: 0x00,
    0x0F: 0x00,
    0xDA: 0xFF,
    0xDB: 0xFF,
    0xDC: 0xFF,
}
# SWRESET leaves MADCTL and COLMOD alone and returns sleep, display and
# inversion to their defaults (pages 151, 153, 155, 157, 159).
READ_AFTER_SWRESET = {**READ_AFTER_RESET, 0x09: 0x24510000, 0x0B: 0x48, 0x0C: 0x55}

# The bytes st7365p_init() sends after its tables: SLPOUT, VSCRDEF (0, 320,
# 160 lines), VSCSAD 0, then st7365p_display_on() sends DISPON.
INIT_TAIL = ["c11", "c33", "d00", "d00", "d01", "d40", "d00", "da0", "c37", "d00", "d00", "c29"]

SPI1_SSPDR = 0x40040008
SPI1_SSPSR = 0x4004000C
SSPSR_TFE, SSPSR_TNF, SSPSR_RNE, SSPSR_BSY = 0x01, 0x02, 0x04, 0x10
FIFO_DEPTH = 8
FRAME_BYTE = 0xA5
SPI_SETTLE_SLICES = 40


def spi_fifo_failures(levels: dict[str, int], accepted: int, dropped: int, sent: int) -> list[str]:
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
    expect(
        one & SSPSR_BSY != 0 and one & SSPSR_TFE != 0,
        "spi1: after one frame BSY must be set and the FIFO empty (the frame is shifting)",
    )
    expect(
        fill & SSPSR_TNF != 0 and fill & SSPSR_TFE == 0,
        f"spi1: with {FIFO_DEPTH - 1} frames queued TNF must be set and TFE clear",
    )
    expect(
        full & SSPSR_TNF == 0 and full & SSPSR_TFE == 0,
        f"spi1: with {FIFO_DEPTH} frames queued TNF must be clear",
    )
    expect(over & SSPSR_TNF == 0, "spi1: TNF must stay clear while the FIFO is full")
    expect(
        idle & SSPSR_BSY == 0 and idle & SSPSR_TNF != 0 and idle & SSPSR_TFE != 0,
        "spi1: after the wire drains BSY must clear and TNF and TFE must set",
    )
    expect(
        accepted == FIFO_DEPTH + 1,
        f"spi1: the panel received {accepted} bytes from {sent} writes; "
        f"expected {FIFO_DEPTH + 1} (a full FIFO discards the rest)",
    )
    expect(
        dropped == accepted,
        f"spi1: {dropped} of {accepted} probe bytes reached the panel with chip select high",
    )
    return failures


# SSPSR values of a controller that behaves as described above, and the
# observation of the model before the FIFO level existed (TNF and TFE
# constant, every write delivered).
GOOD_FIFO = (
    {"one": 0x17, "fill": 0x16, "full": 0x1C, "over": 0x1C, "idle": 0x0F},
    FIFO_DEPTH + 1,
    FIFO_DEPTH + 1,
    FIFO_DEPTH + 4,
)
UNBOUNDED_FIFO = (
    {"one": 0x17, "fill": 0x17, "full": 0x17, "over": 0x17, "idle": 0x0F},
    FIFO_DEPTH + 4,
    FIFO_DEPTH + 4,
    FIFO_DEPTH + 4,
)


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
    for label, count in (
        ("one", 1),
        ("fill", FIFO_DEPTH),
        ("full", FIFO_DEPTH + 1),
        ("over", FIFO_DEPTH + 4),
    ):
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
    print(
        "check-picocalc: spi1 SSPSR "
        + " ".join(f"{label} {value:#04x}" for label, value in levels.items())
        + f"; panel took {accepted} of {sent} writes"
    )
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
            pixel = bytes(pixels[at : at + 3])
            lit = pixel != b"\0\0\0"
            if lit:
                colors.add(pixel)
            if lit != ((x, y) in want):
                wrong += 1
    failures = []
    if wrong:
        failures.append(
            f"picofetch: {wrong} of {LOGO_COLS * 2 * LOGO_ROWS * 3} logo "
            "squares differ from the mirror ball"
        )
    if len(colors) < 6:
        failures.append(
            f"picofetch: the logo shows {len(colors)} colors, expected the "
            "six facet colors and more"
        )
    return failures


def logo_frame(shift: int = 0, mono: bool = False) -> tuple[int, bytes]:
    """A 320-pixel-wide frame holding the logo at row 0, for the self-test."""
    width = 320
    pixels = bytearray(width * LOGO_ROWS * CELL_H * 3)
    palette = [
        (255, 0, 255),
        (92, 92, 255),
        (0, 255, 255),
        (0, 255, 0),
        (255, 255, 0),
        (255, 0, 0),
        (255, 255, 255),
    ]
    for x, y in logo_squares():
        color = palette[0] if mono else palette[((x // 2 + 1) // 2 + y // 3) % 7]
        for line in range(4 if y % 3 == 2 else 3):
            for col in range(3):
                at = (((y // 3) * CELL_H + (y % 3) * 3 + line) * width + (x + shift) * 3 + col) * 3
                pixels[at : at + 3] = bytes(color)
    return width, bytes(pixels)


def check_backlight(run: Run) -> None:
    """Check the boot screen level and a change by an ordinary user.

    /etc/rc writes the level of /etc/backlight, or 160 when the file is
    absent as on a built image, to machdep.picocalc.lcd_backlight; the
    keyboard model keeps what it is given, conditioned as the vendor firmware
    does. backlight, run as operator, must reach the node through its setuid
    mode and print the level the model kept for 88, which is 80. An image
    without the program checks the boot level alone and says so.
    """
    kbd = "sysbus.i2c1.kbd"
    level = monitor_int(run.renode, f"{kbd} BacklightLevel")
    run.expect(
        level == 160,
        f"backlight: the screen level after boot is {level}, expected 160 from /etc/rc",
    )
    console = picocalc_dump.Console(run)
    console.type("ls /usr/bin/backlight || echo light-none")
    run.wait_until(
        lambda: run.has_line("/usr/bin/backlight") or run.has_line("light-none"),
        "backlight probe",
        "the path or light-none",
        KEY_BUDGET_SECONDS,
    )
    if run.has_line("light-none"):
        print(
            "check-picocalc: skip the backlight command: the image does not carry it "
            "(both PicoCalc profiles do)"
        )
        return
    run.type_line("backlight 88")
    run.wait_for_line("lcd 80", "backlight answer as operator", KEY_BUDGET_SECONDS)
    level = monitor_int(run.renode, f"{kbd} BacklightLevel")
    run.expect(
        level == 80, f"backlight: the screen level after 'backlight 88' is {level}, expected 80"
    )
    run.snapshot("backlight")


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
    run.wait_until(
        lambda: run.has_line("/usr/bin/picofetch") or run.has_line("fetch-none"),
        "picofetch probe",
        "the path or fetch-none",
        KEY_BUDGET_SECONDS,
    )
    if run.has_line("fetch-none"):
        print(
            "check-picocalc: skip picofetch and picotop: the image carries neither "
            "(the picocalc profile does)"
        )
        return
    run.type_line("picofetch")
    run.wait_for(["Reset  "], "picofetch report", KEY_BUDGET_SECONDS)
    run.wait_for_prompt("prompt after picofetch", KEY_BUDGET_SECONDS)
    run.snapshot("picofetch")
    for text in (
        "OS       GizmoBSD 0.1.0-alpha",
        "Kernel   PICOCALC",
        "CPU      125.000 MHz, ",
        "SRAM     264K, process window 192K",
        "MPU      on, ",
        "Display  318x320 px, 53x32 cells",
        "Tables   proc ",
        "Kstack   ",
    ):
        run.expect(run.has(text), f"picofetch: no {text!r} on the panel")
    run.expect(
        any(line.startswith("Kstack   ") and " peak " in line for line in run.lines),
        "picofetch: no Kstack peak from kern.capacity on the panel",
    )
    for label in ("Memory  ", "Swap    ", "Root    ", "Battery ", "Flash    image "):
        run.expect(run.has(label), f"picofetch: no {label!r} line on the panel")
    # Renode's ADC is a model, so the line's form is checked and its values
    # are not.
    run.expect(
        any(re.match(r"Sensors  -?\d+\.\d C, VSYS \d+\.\d\d V", line) for line in run.lines),
        "picofetch: no Sensors line with a temperature and VSYS on the panel",
    )
    run.expect(not run.has("read failed"), "picofetch: a sysctl read failed")
    # The title row, not a prompt: a prompt carries the shell's $.
    title = [i for i, line in enumerate(run.lines) if "operator@" in line and "$" not in line]
    if not title:
        run.fail("picofetch: no operator@host title on the panel")
    else:
        width, _, pixels = picocalc_fb.read_ppm(os.path.join(run.outdir, "picofetch.ppm"))
        for message in logo_failures(width, pixels, title[-1]):
            run.fail(message)
    console.type("picotop -c 2 -d 1")
    run.wait_for(["q quit"], "picotop frame", KEY_BUDGET_SECONDS)
    run.wait_until(
        lambda: any(line.rstrip().endswith("$") for line in run.lines[-2:]),
        "prompt after picotop",
        "a shell prompt on the last rows",
        KEY_BUDGET_SECONDS,
    )
    run.snapshot("picotop")
    for text in (
        " picotop  ",
        "CPU  ",
        "Mem  ",
        "Swp  ",
        "/    ",
        "Bat  ",
        "Evt  cs ",
        "CPU%   TIME COMMAND",
    ):
        run.expect(run.has(text), f"picotop: no {text!r} on the panel")
    run.expect(
        any(line.rstrip().endswith("picotop") and " R+ " in line for line in run.lines),
        "picotop: no running, in-core picotop row in the process table",
    )


# A Status line the model printed after a passing boot (st7365p.cs Status()).
GOOD_STATUS = (
    "display on, sleep out, invert on, madctl 0x48, colmod 0x55, "
    "scroll top 0 area 320 bottom 160 start 0, 779602 bytes, 250660 pixels, "
    "0 outside the window, 0 unknown commands, 0 without chip select"
)


def read_stream(cmd: int, value: int, dummy: bool = True) -> list[int]:
    """The frames a panel that follows the datasheet returns for a read command."""
    _, cycles, bits = READ_COMMANDS[cmd]
    stream = "0" * (cycles if dummy else 0) + format(value, f"0{bits}b")
    stream += "0" * (-len(stream) % 8)
    return [int(stream[i : i + 8], 2) for i in range(0, len(stream), 8)]


class TimelineRun(Run):
    """A Run whose panel follows a function of virtual time, with no emulator."""

    def __init__(self, shown: Callable[[float], list[str]], outdir: str = "") -> None:
        super().__init__(typing.cast(Renode, None), outdir)
        self.shown = shown

    def advance(self, seconds: float = SLICE_SECONDS) -> list[str]:
        self.virtual += seconds
        return self.show(self.shown(round(self.virtual, 3)))


def banner_window(start: float, banner: str, width: float = 0.3) -> Callable[[float], list[str]]:
    """A panel blank until start, showing the banner for width seconds, then scrolled."""

    def shown(virtual: float) -> list[str]:
        if virtual < start:
            return [""]
        if virtual < start + width:
            return [banner, "lcd0: ST7365P 320x320 on SPI1"]
        return ["        exec_check(): read header"]

    return shown


def wait_outcomes(
    wait: Callable[[Run], object], banner: str, starts: list[float]
) -> dict[float, bool]:
    """Whether a banner wait passes for each start of the banner's window."""
    outcome = {}
    for start in starts:
        run = TimelineRun(banner_window(start, banner))
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            try:
                wait(run)
                outcome[start] = True
            except StageFailed:
                outcome[start] = False
    return outcome


BANNER_STARTS = [step / 20 for step in range(0, 41)]


def sampling_failures(
    wait_banner: typing.Optional[Callable[[Run], object]] = None,
    banner: str = "GizmoBSD 0.1.0-alpha (PICOCALC_SD_DIAG)",
) -> list[str]:
    """Calibrate the cadence of a wait for a boot line against windows at every phase.

    The line is on the panel for 0.3 virtual seconds and starts anywhere in the
    first two seconds. A wait on the current frame at SLICE_SECONDS steps (wait_for) must
    pass for a window that holds a sample and fail for one that holds zero samples, which
    proves the windows can miss; the wait under test (wait_seen unless a caller
    supplies the wait its cases use) must pass for every start."""

    def coarse(run: Run) -> object:
        return run.wait_for([banner], "banner", BOOT_BUDGET_SECONDS)

    def fine(run: Run) -> object:
        return run.wait_seen([banner], "banner", BOOT_BUDGET_SECONDS)

    failures = []
    bad = wait_outcomes(coarse, banner, BANNER_STARTS)
    if not bad[0.5] or bad[0.55]:
        failures.append(
            "the wait on the current frame must pass for a window starting at 0.50 "
            "and fail for one starting at 0.55"
        )
    for name, wait in (("wait_seen", fine), ("the wait the cases use", wait_banner)):
        if wait is None:
            continue
        missed = [
            start for start, ok in wait_outcomes(wait, banner, BANNER_STARTS).items() if not ok
        ]
        if missed:
            failures.append(f"{name} missed a window of the line starting at {missed[:4]}")

    def two_lines(virtual: float) -> list[str]:
        if 0.5 <= virtual < 0.8:
            return ["first line"]
        if 1.5 <= virtual < 1.8:
            return ["second line"]
        return [""]

    run = TimelineRun(two_lines)
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        try:
            run.wait_seen(["first line", "second line"], "two lines", BOOT_BUDGET_SECONDS)
        except StageFailed:
            failures.append("wait_seen missed two texts that appear in different frames")
    return failures


def frame_record_failures() -> list[str]:
    """Calibrate the kept frames: the first, a marker for the gap and the last."""
    failures = []
    with tempfile.TemporaryDirectory() as outdir:
        run = TimelineRun(banner_window(1.0, "banner line"), outdir)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            try:
                run.wait_seen(["never shown"], "unseen text", BOOT_BUDGET_SECONDS)
            except StageFailed:
                pass
        with open(os.path.join(outdir, "frames.txt"), encoding="utf-8") as handle:
            kept = handle.read().split("\n")
        taken = run.frames_taken
        want_gap = f"frames {FRAMES_KEPT_HEAD + 1} to {taken - FRAMES_KEPT_TAIL} are not kept"
        if kept[0] != "frame 1 t=0.1s" or "  (blank)" not in kept[1:2]:
            failures.append(f"frames.txt starts with {kept[:2]!r}")
        if want_gap not in kept:
            failures.append(f"frames.txt lacks the line {want_gap!r}")
        if "  banner line" not in kept:
            failures.append("frames.txt lost the banner window inside the kept head")
        if f"frame {taken} t={run.virtual:.1f}s" not in kept:
            failures.append("frames.txt lost the last frame")
        listed = sum(1 for line in kept if line.startswith("frame "))
        if listed != FRAMES_KEPT_HEAD + FRAMES_KEPT_TAIL:
            failures.append("frames.txt keeps a number of frames other than head plus tail")
    return failures


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
            print(f"self-test: the {name} Status line was not rejected: {broken}", file=sys.stderr)
            return 1
    levels, accepted, dropped, sent = GOOD_FIFO
    if spi_fifo_failures(levels, accepted, dropped, sent):
        print("self-test: the recorded good FIFO observation failed", file=sys.stderr)
        return 1
    levels, accepted, dropped, sent = UNBOUNDED_FIFO
    if len(spi_fifo_failures(levels, accepted, dropped, sent)) < 3:
        print("self-test: the unbounded FIFO observation was not rejected", file=sys.stderr)
        return 1
    for state, want in (
        (STATE_RESET, READ_AFTER_RESET),
        (STATE_INIT, READ_AFTER_INIT),
        (STATE_SWRESET, READ_AFTER_SWRESET),
    ):
        good = {cmd: decode_read(cmd, read_stream(cmd, value)) for cmd, value in want.items()}
        if read_failures(state, good, want):
            print(f"self-test: the recorded good {state} readback failed", file=sys.stderr)
            return 1
    good = {
        cmd: decode_read(cmd, read_stream(cmd, value)) for cmd, value in READ_AFTER_INIT.items()
    }
    read_mutants = {
        "a model that answers zeros": {cmd: 0 for cmd in good},
        "a read without the dummy cycle": {
            cmd: decode_read(cmd, read_stream(cmd, value, dummy=False))
            for cmd, value in READ_AFTER_INIT.items()
        },
        "a sleeping panel": {**good, 0x0A: 0x08},
        "an unprogrammed MADCTL": {**good, 0x0B: 0x00},
        "a diagnostic result with a reserved bit set": {**good, 0x0F: 0x20},
    }
    for name, values in read_mutants.items():
        if not read_failures(STATE_INIT, values, READ_AFTER_INIT):
            print(f"self-test: {name} was not rejected by the readback check", file=sys.stderr)
            return 1
    diagnosed = {**good, 0x0F: 0xC0}
    if read_failures(STATE_INIT, diagnosed, READ_AFTER_INIT):
        print(
            "self-test: a diagnostic result with D7 and D6 set was rejected after SLPOUT",
            file=sys.stderr,
        )
        return 1
    undone = {
        cmd: decode_read(cmd, read_stream(cmd, value)) for cmd, value in READ_AFTER_RESET.items()
    }
    if not read_failures(STATE_RESET, {**undone, 0x0F: 0xC0}, READ_AFTER_RESET):
        print(
            "self-test: a diagnostic result with D7 and D6 set was accepted after a reset",
            file=sys.stderr,
        )
        return 1
    try:
        read_failures("init", good, READ_AFTER_INIT)
    except ValueError:
        pass
    else:
        print("self-test: a readback state no stage uses was not refused", file=sys.stderr)
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
    bound = picocalc_dump.message_bound_self_test()
    if bound:
        print(f"self-test: the dump message bound misjudged {bound}", file=sys.stderr)
        return 1
    # The fallback line wrapped at the panel's 53 cells, as the console
    # draws it, must pass; an attach, a missing line and a fallback printed
    # by another driver must not.
    fallback = "psram0: id 0/0 is not an ESP-PSRAM64H, swap stays on fl1"
    wrapped = [fallback[: picocalc_fb.COLS], fallback[picocalc_fb.COLS :]]
    for name, lines in (
        ("wrapped id refusal", ["kbd0: x"] + wrapped),
        ("stall refusal", ["psram0: not usable (error 4), swap stays on fl1"]),
    ):
        if psram_failures(lines) or not psram_probe_seen(lines):
            print(f"self-test: the {name} psram0 line failed", file=sys.stderr)
            return 1
    for name, lines in (
        ("attach", ["psram0: ESP-PSRAM64H 8192 kbytes on PIO0, swap"]),
        ("missing", ["kbd0: keyboard controller at I2C1 0x1f, firmware 1.6"]),
        ("other driver", ["sd0: not usable (error 4), swap stays on fl1"]),
    ):
        if not psram_failures(lines):
            print(f"self-test: the {name} psram0 case was not rejected", file=sys.stderr)
            return 1
    problems = sampling_failures() + frame_record_failures()
    if problems:
        for problem in problems:
            print(f"self-test: {problem}", file=sys.stderr)
        return 1
    print(
        "check-picocalc self-test: ok (good line, sleeping panel and four other mutants, "
        "good and unbounded FIFO, three good readbacks and five mutants, "
        "good, shifted and one-color logo, psram0 fallback and three mutants, "
        "12-, 255- and 256-byte dump messages, banner windows at 41 phases against the "
        "current-frame wait and the counted frames, the kept frames of a failed wait)"
    )
    return 0


def main_readback(argv: list[str]) -> int:
    """Only the panel read commands: the machine is built and never started."""
    renode_path, unix_bin, flash_bin, outdir = argv
    os.makedirs(outdir, exist_ok=True)
    renode = Renode(renode_path, os.path.join(outdir, "renode.log"), 600.0)
    run = Run(renode, outdir)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'picocalc-machine.resc')}")
        check_panel_readback(run)
        check_panel_memory(run)
    except RenodeError as error:
        print(f"check-picocalc: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    if run.failures:
        return 1
    print("check-picocalc: readback pass")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--self-test"]:
        return self_test()
    if argv[:1] == ["--readback"] and len(argv) == 5:
        return main_readback(argv[1:])
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
        check_panel_readback(run)
        check_panel_memory(run)
        renode.command(f"sysbus.uart0 CreateFileBackend @{os.path.join(outdir, 'uart0.txt')} true")
        init_trace = expected_init_trace()
        renode.command(f"sysbus.spi1.lcd TraceBytes {len(init_trace)}")
        run.wait_seen(BANNER_LINES, "banner", BOOT_BUDGET_SECONDS)
        run.wait_until(
            lambda: psram_probe_seen(run.lines),
            "psram probe",
            "a psram0 outcome line",
            BOOT_BUDGET_SECONDS,
        )
        run.snapshot("banner")
        for message in psram_failures(run.lines):
            run.fail(message)
        if card is not None:
            picocalc_dump.check_swap_boot(run)
        else:
            run.wait_until(
                lambda: run.saw(SWAP_ON_FL1),
                "swap device after the psram probe",
                repr(SWAP_ON_FL1),
                BOOT_BUDGET_SECONDS,
            )
        check_panel_registers(run)
        # The monitor echoes prompts and command lines around its output.
        traced = [
            line
            for line in renode.command("sysbus.spi1.lcd Trace").splitlines()
            if re.fullmatch(r"\s*([cd][0-9a-f]{2}\s*)+", line)
        ]
        seen = traced[-1].split() if traced else []
        run.expect(
            seen == init_trace,
            f"panel: init bytes differ from st7365p.c's startup tables: {seen[:12]}",
        )
        run.wait_for(["login:"], "login prompt", LOGIN_BUDGET_SECONDS)
        run.snapshot("login")
        # The operator account has an empty password (the image checker
        # verifies it), so the name alone reaches a shell.
        run.type_line("operator")
        run.wait_for_prompt("shell prompt", KEY_BUDGET_SECONDS)
        run.type_line("uname -sr", erase_last=True)
        run.wait_for_line(
            "GizmoBSD 0.1.0-alpha",
            "uname answer typed through the keyboard model",
            KEY_BUDGET_SECONDS,
        )
        run.snapshot("shell")
        # Nine keys to log in and twelve for the command (the wrong character and its
        # Backspace included), a press and a release each.
        queued = monitor_int(renode, "sysbus.i2c1.kbd Queued")
        delivered = monitor_int(renode, "sysbus.i2c1.kbd Delivered")
        dropped = monitor_int(renode, "sysbus.i2c1.kbd Dropped")
        run.expect(
            queued == 42 and delivered == 42 and dropped == 0,
            f"keyboard: {queued} events queued, {delivered} delivered through "
            f"register 0x09, {dropped} dropped; expected 42, 42 and 0",
        )
        transactions = monitor_int(renode, "sysbus.i2c1.kbd Transactions")
        run.expect(transactions > 0, "keyboard: no I2C transaction reached 0x1f")
        check_spi_fifo(run)
        check_monitors(run)
        check_backlight(run)
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
