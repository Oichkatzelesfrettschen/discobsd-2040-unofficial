# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Run the LCD_GFX_EXT panel primitives of a PicoCalc kernel on Unicorn.

The PICOCALC_SD kernel's lcd_gfx_power, st7365p_abort, lcd_gfx_read,
lcd_gfx_scanline and lcd_gfx_dither run against a model of the ST7365P on
SPI1: every byte written to the PL022 data register is decoded as a command
(DC low) or a parameter (DC high) under chip select, and the receive FIFO
returns the panel's reply byte for each byte clocked, as the PL022 shifts
one in per byte out. Each case states the command and parameter stream the
datasheet requires (ST7365P pages 163-165 for sleep, 196 for COLMOD, 178 for
RAMRD, 201 for GSCAN, 229 for MODESEL behind the Command Table 2 unlock) and
the delays the driver must request. Machine-code and table mutants follow:
each must fail its case, so a pass comes from a gate that reads the traffic.
"""

import argparse
from pathlib import Path

from kernel_emulator import BX_LR, KernelEmulator, KernelImage, literal_words
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0

SPI1 = 0x40040000
SIO = 0xD0000000
TIMER = 0x40054000
LCD_CS = 13
LCD_DC = 14

PWR_ON, PWR_IDLE, PWR_OFF, PWR_SLEEP = range(4)
SLPIN, SLPOUT, DISPOFF, DISPON = 0x10, 0x11, 0x28, 0x29
CASET, RASET, RAMWR, RAMRD = 0x2A, 0x2B, 0x2C, 0x2E
IDMOFF, IDMON, COLMOD, GSCAN = 0x38, 0x39, 0x3A, 0x45
GRAM_LINES = 480
DITHER_N, DITHER_X, DITHER_BYTE = 8, 8, 0x82

# Leaving a mode (page 165 SLPOUT, 38h IDMOFF, 29h DISPON) and entering one.
LEAVE = {PWR_IDLE: IDMOFF, PWR_OFF: DISPON, PWR_SLEEP: SLPOUT}
ENTER = {PWR_IDLE: IDMON, PWR_OFF: DISPOFF, PWR_SLEEP: SLPIN}
SLEEP_MS = 120  # SLPIN and SLPOUT: 120 ms before the other of the pair
# Command Table 2 unlock (F0h C3h, F0h 96h), MODESEL 02h, then the lock.
MODESEL_UNLOCKED = (
    (0xF0, (0xC3,)),
    (0xF0, (0x96,)),
    (0xB9, (0x02,)),
    (0xF0, (0x3C,)),
    (0xF0, (0x69,)),
)


class Panel:
    """The ST7365P on SPI1 as the driver sees it, with a token log.

    Tokens: ("cmd", byte) and ("data", byte) for bytes clocked with chip
    select low, ("bus", byte) for a byte clocked with it high, ("cs", level)
    on each change of chip select, ("delay", ms) for each mdelay().
    """

    def __init__(self, image, patches=()):
        self.image = image
        delay = image.function("mdelay").address
        self.machine = KernelEmulator(image, [(delay, BX_LR), *patches])
        cpu = self.machine.uc
        cpu.mmio_map(SPI1, 0x1000, self.spi_read, None, self.spi_write, None)
        cpu.mmio_map(SIO, 0x1000, self.sio_read, None, self.sio_write, None)
        cpu.mmio_map(TIMER, 0x1000, self.timer_read, None, self.ignore, None)
        cpu.hook_add(UC_HOOK_CODE, self.delay, begin=delay, end=delay)
        self.cs = 1
        self.dc = 1
        self.rx = []
        self.reply = []
        self.micros = 0
        self.tokens = []
        self.reply_for = {}

    def ignore(self, *args):
        pass

    def timer_read(self, cpu, offset, size, data):
        self.micros += 7
        return self.micros if offset == 0x28 else 0

    def sio_read(self, cpu, offset, size, data):
        return 0

    def sio_write(self, cpu, offset, size, value, data):
        if offset not in (0x14, 0x18):
            return
        level = 1 if offset == 0x14 else 0
        if value & (1 << LCD_CS) and level != self.cs:
            self.cs = level
            self.tokens.append(("cs", level))
        if value & (1 << LCD_DC):
            self.dc = level

    def spi_read(self, cpu, offset, size, data):
        if offset == 0x0C:  # SSPSR: TFE, TNF, RNE when a byte waits
            return 0x3 | (0x4 if self.rx else 0)
        if offset == 0x08:
            return self.rx.pop(0) if self.rx else 0
        return 0

    def spi_write(self, cpu, offset, size, value, data):
        if offset != 0x08:
            return
        byte = value & 0xFF
        if self.cs:
            self.tokens.append(("bus", byte))
            self.rx.append(0xFF)
            return
        if self.dc == 0:
            self.tokens.append(("cmd", byte))
            self.reply = list(self.reply_for.get(byte, ()))
            self.rx.append(0xFF)
        else:
            self.tokens.append(("data", byte))
            self.rx.append(self.reply.pop(0) if self.reply else 0xFF)

    def delay(self, cpu, address, size, data):
        self.tokens.append(("delay", cpu.reg_read(UC_ARM_REG_R0)))

    def symbol(self, prefix):
        found = [
            s
            for name, symbols in self.image.symbols.items()
            if name == prefix or name.startswith(prefix + ".")
            for s in symbols
            if s.kind == "STT_OBJECT"
        ]
        if len(found) != 1:
            raise ValueError(f"{len(found)} objects named {prefix}")
        return found[0].address

    def set_word(self, prefix, value):
        self.machine.write(self.symbol(prefix), value.to_bytes(4, "little"))

    def get_word(self, prefix):
        return self.machine.word(self.symbol(prefix))

    def call(self, name, *args):
        self.tokens.clear()
        return self.machine.call(name, *args, policy=None, budget=400000)


def framed(*parts):
    """Expected tokens of one chip-select frame of commands and parameters."""
    out = [("cs", 0)]
    for cmd, params in parts:
        out.append(("cmd", cmd))
        out.extend(("data", b) for b in params)
    out.append(("cs", 1))
    return out


def power_expected(was, mode):
    if was == mode:
        return []
    out = [("cs", 0)]
    if was != PWR_ON:
        out.append(("cmd", LEAVE[was]))
        if was == PWR_SLEEP:
            out += [("cs", 1), ("delay", SLEEP_MS), ("cs", 0)]
    if mode != PWR_ON:
        out.append(("cmd", ENTER[mode]))
    out.append(("cs", 1))
    if mode == PWR_SLEEP:
        out.append(("delay", SLEEP_MS))
    return out


def wake_expected():
    return (
        [("delay", SLEEP_MS)]
        + framed((SLPOUT, ()))
        + [("delay", SLEEP_MS)]
        + framed((IDMOFF, ()), (DISPON, ()))
    )


def check_power(panel):
    failures = []
    for was in range(4):
        for mode in range(4):
            for pending in (0, 1):
                panel.set_word("lcd_power_mode", was)
                panel.set_word("lcd_power_pending", pending)
                panel.call("lcd_gfx_power", mode)
                got = panel.tokens[:]
                want = power_expected(was, mode)
                end_pending = pending if was == mode else int(mode != PWR_ON)
                if got != want:
                    failures.append(f"power {was}->{mode}: {got} != {want}")
                if panel.get_word("lcd_power_mode") != mode:
                    failures.append(f"power {was}->{mode}: mode not recorded")
                if panel.get_word("lcd_power_pending") != end_pending:
                    failures.append(f"power {was}->{mode}: pending flag wrong")
    return failures


def check_abort(panel):
    failures = []
    for mode in range(4):
        panel.set_word("lcd_power_mode", PWR_ON)
        panel.set_word("lcd_power_pending", 0)
        panel.call("lcd_gfx_power", mode)
        panel.call("st7365p_abort")
        # st7365p_abort() first raises chip select, which is already high
        # after a completed change, so the stream starts with the wake.
        want = wake_expected() if mode != PWR_ON else []
        if panel.tokens != want:
            failures.append(f"abort after mode {mode}: {panel.tokens} != {want}")
        if panel.get_word("lcd_power_mode") != PWR_ON or panel.get_word("lcd_power_pending"):
            failures.append(f"abort after mode {mode}: panel not recorded as on")
    # A fault inside a mode change: the recorded mode still reads on while
    # the flag says a change may have begun, so the wake goes out; with the
    # flag clear the recorded mode alone sends nothing.
    for mode, pending, wake in ((PWR_ON, 1, True), (PWR_SLEEP, 0, False)):
        panel.set_word("lcd_power_mode", mode)
        panel.set_word("lcd_power_pending", pending)
        panel.call("st7365p_abort")
        want = wake_expected() if wake else []
        if panel.tokens != want:
            failures.append(f"abort with mode {mode} pending {pending}: {panel.tokens}")
    return failures


def check_read(panel):
    failures = []
    buffer = panel.machine.alloc(64)
    for x, y, n, length in ((0, 0, 1, 4), (17, 300, 5, 16), (319, 479, 1, 3)):
        pixels = bytes((7 * i + x + y) & 0xFF for i in range(length))
        panel.reply_for = {RAMRD: pixels}
        panel.machine.write(buffer, b"\x5a" * 64)
        panel.call("lcd_gfx_read", x, y, n, buffer, length)
        panel.reply_for = {}
        want = framed(
            (CASET, (x >> 8, x & 0xFF, (x + n - 1) >> 8, (x + n - 1) & 0xFF)),
            (RASET, (y >> 8, y & 0xFF, y >> 8, y & 0xFF)),
            (RAMRD, (0,) * length),
        )
        if panel.tokens != want:
            failures.append(f"read {x},{y},{n}: {panel.tokens} != {want}")
        got = panel.machine.read(buffer, 64)
        if got[:length] != pixels or got[length:] != b"\x5a" * (64 - length):
            failures.append(f"read {x},{y},{n}: stored {got[:length].hex()} for {pixels.hex()}")
    return failures


def check_scanline(panel):
    failures = []
    for line in (0, 1, 2, 159, 319, 320, 479, 0x1FF):
        # One dummy clock, the 16-bit line MSB first, then idle bits.
        wire = (line << 7).to_bytes(3, "big")
        panel.reply_for = {GSCAN: wire}
        got = panel.call("lcd_gfx_scanline")
        panel.reply_for = {}
        if panel.tokens != framed((GSCAN, (0, 0, 0))):
            failures.append(f"scanline {line}: traffic {panel.tokens}")
        if got != line:
            failures.append(f"scanline {line}: returned {got}")
    return failures


def check_dither(panel):
    failures = []
    for on in (0, 1):
        panel.call("lcd_gfx_dither", on)
        x1 = DITHER_X + DITHER_N - 1
        y = GRAM_LINES - 1
        want = framed(
            (COLMOD, (0x77,)),
            *(MODESEL_UNLOCKED if on else ()),
            (CASET, (DITHER_X >> 8, DITHER_X & 0xFF, x1 >> 8, x1 & 0xFF)),
            (RASET, (y >> 8, y & 0xFF, y >> 8, y & 0xFF)),
            (RAMWR, (DITHER_BYTE,) * (3 * DITHER_N)),
        )
        if panel.tokens != want:
            failures.append(f"dither {on}: {panel.tokens} != {want}")
    return failures


CASES = (
    ("power", check_power),
    ("abort", check_abort),
    ("read", check_read),
    ("scanline", check_scanline),
    ("dither", check_dither),
)


def run(image, patches=()):
    results = {}
    for name, check in CASES:
        results[name] = check(Panel(image, patches))
    return results


def object_address(image, prefix):
    found = [
        s
        for name, symbols in image.symbols.items()
        if name == prefix or name.startswith(prefix + ".")
        for s in symbols
        if s.kind == "STT_OBJECT"
    ]
    if len(found) != 1:
        raise ValueError(f"{len(found)} objects named {prefix}")
    return found[0]


def zero_word(image):
    """An aligned flash word holding zero, which no store can change."""
    for base, data, _ in image.segments:
        if not 0x10000000 <= base < 0x10200000:
            continue
        for offset in range((-base) % 4, len(data) - 3, 4):
            if data[offset : offset + 4] == bytes(4):
                return base + offset
    raise ValueError("no zero word in flash")


def mutants(image):
    """(name, case it must fail, patches)."""
    enter = object_address(image, "enter")
    leave = object_address(image, "leave")
    modesel = object_address(image, "modesel")
    entered = image.read(enter.address, enter.size)
    unlock = image.read(modesel.address, modesel.size)
    pending = object_address(image, "lcd_power_pending").address
    zero = zero_word(image)
    slots = [
        slot for slot, word in literal_words(image, "st7365p_abort").items() if word == pending
    ]
    if len(slots) != 1:
        raise ValueError(f"st7365p_abort loads lcd_power_pending from {len(slots)} slots")
    return (
        ("power-silent", "power", [(image.function("lcd_gfx_power").address, BX_LR)]),
        (
            "power-idle-off-swapped",
            "power",
            [(enter.address, bytes((entered[0], entered[2], entered[1], entered[3])))],
        ),
        (
            "power-leave-sleep-as-on",
            "power",
            [(leave.address + PWR_SLEEP, bytes((DISPON,)))],
        ),
        # st7365p_abort reads its pending flag from a word that stays zero.
        ("abort-flag-unseen", "abort", [(slots[0], zero.to_bytes(4, "little"))]),
        ("read-silent", "read", [(image.function("lcd_gfx_read").address, BX_LR)]),
        (
            "scanline-zero",
            "scanline",
            [(image.function("lcd_gfx_scanline").address, b"\x00\x20" + BX_LR)],
        ),
        (
            "dither-no-unlock",
            "dither",
            [(modesel.address, bytes((0x00,)) + unlock[1:])],
        ),
        ("dither-modesel-off", "dither", [(modesel.address + 8, b"\x00")]),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    image = KernelImage(args.elf)
    results = run(image)
    bad = [f"{name}: {line}" for name, lines in results.items() for line in lines]
    if bad:
        print("\n".join(bad[:20]))
        raise SystemExit(f"FAIL panel LCD_GFX_EXT primitives: {len(bad)} failures")
    for label, case, patches in mutants(image):
        try:
            failed = run(image, patches)[case]
        except Exception as error:  # a crashed mutant is no verdict
            raise SystemExit(f"FAIL mutant {label} did not run: {error}") from None
        if not failed:
            raise SystemExit(f"FAIL mutant {label} passed the {case} case")
        print(f"mutant {label} rejected by {case} ({len(failed)} failures)")
    print(
        "PASS panel LCD_GFX_EXT primitives: 32 power transitions, abort wake "
        "after each mode, 3 reads, 8 scanlines, dither off and on"
    )


if __name__ == "__main__":
    main()
