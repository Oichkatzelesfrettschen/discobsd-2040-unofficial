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

"""Clock-derived constants of the RP2040 port at a chosen clk_sys.

usage: clockcalc.py table [--peri PERI_MHZ] [SYS_MHZ ...]
       clockcalc.py pll TARGET_MHZ
       clockcalc.py selftest

Mirrors the formulas of the shipped drivers (docs/research/
rp2040-clock-configuration.md, "Every consumer of a clock-derived
constant"), so that a candidate clk_sys is judged against the same integer
arithmetic the kernel evaluates at compile time. 'table' prints one row per
consumer and clock; a row marked FAIL names a constraint the driver would
violate (a static assertion in the tree, or a datasheet limit). 'pll' lists
the exact PLL_SYS settings for a 12 MHz crystal within the limits of RP2040
datasheet 2.18.2. 'selftest' checks the 125 MHz rows against the values the
tree documents (known-good) and requires the validators to reject four
defective inputs (known-bad).
"""

import sys

XTAL = 12_000_000
HZ = 1000
UART_BAUD = 115200
PCM_RATE = 8000
PCM_LEVELS = 250
PSRAM_CLKDIV = 2
LCD_PRESCALE = 2
LCD_SCR = 4
KBD_I2C_HZ = 100_000
BELL_HZ = 1000
USB_HZ = 48_000_000

# Frame lengths in clk_sys cycles at PSRAM_CLKDIV 2, from the 125 MHz model
# times in memory-devices.md (4.45 us write, 4.72 us read, 5.2 us worst
# masked frame of 64 bytes).
PSRAM_FRAME_CYCLES = {"write": 556, "read": 590, "worst": 650}
PSRAM_TCEM_US = 8.0
LCD_TSCYCW_NS = 66.0
FLASH_MAX_HZ = 133_000_000  # W25Q16JV, 3.0 to 3.6 V
SD_DEFAULT_SPEED_HZ = 25_000_000


def pll_check(refdiv, fbdiv, pd1, pd2, vco_min=750e6, vco_max=1600e6):
    """Return the violated datasheet 2.18.2 constraints, as strings."""
    bad = []
    fref = XTAL / refdiv
    vco = fref * fbdiv
    if not 1 <= refdiv <= 63:
        bad.append("REFDIV 1..63")
    if fref < 5e6:
        bad.append("FREF/REFDIV >= 5 MHz")
    if not 16 <= fbdiv <= 320:
        bad.append("FBDIV 16..320")
    if not (1 <= pd1 <= 7 and 1 <= pd2 <= 7):
        bad.append("POSTDIV 1..7")
    if not vco_min <= vco <= vco_max:
        bad.append("VCO 750..1600 MHz")
    if bad == [] and fref > vco / 16:
        bad.append("FREF <= VCO/16")
    return bad


def pll_search(target_hz):
    """Exact (refdiv, fbdiv, pd1, pd2, vco_hz) settings, lowest VCO first."""
    found = []
    for refdiv in range(1, 3):
        for fbdiv in range(16, 321):
            for pd1 in range(1, 8):
                for pd2 in range(1, pd1 + 1):
                    if pll_check(refdiv, fbdiv, pd1, pd2):
                        continue
                    vco = XTAL * fbdiv // refdiv
                    if (XTAL * fbdiv) % (refdiv * pd1 * pd2) == 0 and XTAL * fbdiv // (
                        refdiv * pd1 * pd2
                    ) == target_hz:
                        found.append((vco, refdiv, fbdiv, pd1, pd2))
    found.sort(key=lambda t: (t[0], t[1], -t[3]))
    return [(r, f, a, b, v) for v, r, f, a, b in found]


def uart(peri):
    div = 4 * peri // UART_BAUD
    ibrd, fbrd = div >> 6, div & 0x3F
    actual = 4 * peri / div
    return ibrd, fbrd, actual, (actual / UART_BAUD - 1) * 100


def sd(peri):
    scr = (peri + 800_000 - 1) // 800_000 - 1
    slow = peri / (2 * (scr + 1))
    pre = (peri + 12_500_000 - 1) // 12_500_000
    fast = peri / pre
    ok = 2 <= pre <= 254 and pre % 2 == 0 and scr <= 255
    return scr, slow, pre, fast, ok


def lcd(peri):
    rate = peri / (LCD_PRESCALE * (1 + LCD_SCR))
    return rate, 1e9 / rate >= LCD_TSCYCW_NS


def pcm(sys_hz):
    count = sys_hz // PCM_RATE
    exact = sys_hz % PCM_RATE == 0 and 2 <= count <= 65536
    carrier = sys_hz / PCM_LEVELS
    return count, exact, carrier, carrier == 500_000


def i2c(sys_hz):
    period = (sys_hz + KBD_I2C_HZ // 2) // KBD_I2C_HZ
    lcnt = period * 3 // 5
    hcnt = period - lcnt
    spk = 1 if lcnt < 16 else lcnt // 16
    scl = sys_hz / (lcnt + 1 + hcnt + spk + 7)
    hold_cycles = (sys_hz // 1000 * 3) // 10000 + 1
    return scl, hold_cycles * 1e9 / sys_hz


def psram(sys_hz):
    sck = sys_hz / (2 * PSRAM_CLKDIV)
    worst_us = PSRAM_FRAME_CYCLES["worst"] / sys_hz * 1e6
    return sck, worst_us, worst_us < PSRAM_TCEM_US


def flash(sys_hz):
    sck = sys_hz / 2
    return sck, sck <= FLASH_MAX_HZ


def usb_ok(sys_hz):
    return sys_hz >= USB_HZ * 1.10


def rows(sys_hz, peri_hz=None):
    peri = sys_hz if peri_hz is None else peri_hz
    out = []
    ib, fb, act, err = uart(peri)
    out.append(
        ("uart 115200", "IBRD %d FBRD %d, %.1f Bd, %+.4f%%" % (ib, fb, act, err), abs(err) < 2.0)
    )
    scr, slow, pre, fast, ok = sd(peri)
    out.append(("sd slow", "SCR %d, %.0f Hz" % (scr, slow), slow <= 400_000))
    out.append(("sd fast", "CPSDVSR %d, %.3f MHz" % (pre, fast / 1e6), ok))
    rate, lok = lcd(peri)
    out.append(("lcd spi", "%.3f MHz, %.1f ns" % (rate / 1e6, 1e9 / rate), lok))
    cnt, exact, car, c500 = pcm(sys_hz)
    out.append(("pcm sample", "TOP+1 %d, exact %s" % (cnt, exact), exact))
    out.append(("pcm carrier", "%.1f kHz" % (car / 1e3), c500))
    scl, hold = i2c(sys_hz)
    out.append(
        ("kbd i2c", "SCL %.1f kHz, SDA hold %.0f ns" % (scl / 1e3, hold), 85e3 <= scl <= 100e3)
    )
    sck, wus, wok = psram(sys_hz)
    out.append(("psram", "SCK %.3f MHz, worst frame %.2f us" % (sck / 1e6, wus), wok))
    fsck, fok = flash(sys_hz)
    out.append(("flash xip", "SCK %.2f MHz" % (fsck / 1e6), fok))
    out.append(
        ("systick", "RVR %d, exact %s" % (sys_hz // HZ - 1, sys_hz % HZ == 0), sys_hz % HZ == 0)
    )
    out.append(("usb", "clk_sys/clk_usb %.3f" % (sys_hz / USB_HZ), usb_ok(sys_hz)))
    return out


def table(points, peri_mhz=None):
    for mhz in points:
        sys_hz = int(round(float(mhz) * 1e6))
        peri_hz = None if peri_mhz is None else int(round(float(peri_mhz) * 1e6))
        print(
            "clk_sys %.3f MHz, clk_peri %.3f MHz"
            % (sys_hz / 1e6, (sys_hz if peri_hz is None else peri_hz) / 1e6)
        )
        for name, text, ok in rows(sys_hz, peri_hz):
            print("  %-12s %-44s %s" % (name, text, "ok" if ok else "FAIL"))


def expect(cond, what):
    if not cond:
        raise SystemExit("selftest: " + what)


def selftest():
    # Known-good: the 125 MHz rows equal the values the tree documents.
    expect(pll_check(1, 125, 6, 2) == [], "125 MHz PLL setting rejected")
    expect(pll_check(1, 100, 5, 5) == [], "48 MHz USB PLL setting rejected")
    ib, fb, _, _ = uart(125_000_000)
    expect((ib, fb) == (67, 52), "UART divisor at 125 MHz")
    scr, slow, pre, fast, ok = sd(125_000_000)
    expect((scr, pre, ok) == (156, 10, True), "SD divisors at 125 MHz")
    expect(round(slow) == 398_089 and fast == 12_500_000, "SD rates at 125 MHz")
    expect(lcd(125_000_000)[0] == 12_500_000, "LCD rate at 125 MHz")
    cnt, exact, _, c500 = pcm(125_000_000)
    expect((cnt, exact, c500) == (15625, True, True), "PCM at 125 MHz")
    expect(round(i2c(125_000_000)[0] / 1e3, 1) == 95.9, "I2C SCL at 125 MHz")
    expect(psram(125_000_000)[0] == 31_250_000, "PSRAM SCK at 125 MHz")
    expect(all(ok for _, _, ok in rows(125_000_000)), "a 125 MHz row fails")
    # 133 MHz: the two assertions the benchmark session hit must fail here.
    bad133 = [n for n, _, ok in rows(133_000_000) if not ok]
    expect(sorted(bad133) == ["pcm carrier", "sd fast"], "133 MHz verdicts: %r" % bad133)
    # Known-bad inputs, each of which a validator must reject.
    expect("VCO 750..1600 MHz" in pll_check(1, 134, 6, 2), "VCO 1608 MHz accepted")
    expect("POSTDIV 1..7" in pll_check(1, 125, 8, 2), "POSTDIV1 8 accepted")
    expect("FREF/REFDIV >= 5 MHz" in pll_check(3, 133, 6, 1), "FREF 4 MHz accepted")
    expect(sd(133_000_000)[2] % 2 == 1 and not sd(133_000_000)[4], "odd CPSDVSR accepted")
    expect(not usb_ok(48_000_000), "48 MHz clk_sys accepted with USB")
    expect(pll_search(133_000_000)[0][:4] == (2, 133, 6, 1), "lowest-VCO 133 MHz setting")
    print("selftest: known-good rows match the tree, known-bad inputs are rejected")


def main(argv):
    if len(argv) >= 2 and argv[1] == "table":
        args = argv[2:]
        peri = None
        if args[:1] == ["--peri"] and len(args) >= 2:
            peri, args = args[1], args[2:]
        table(args or ["125", "133"], peri)
    elif len(argv) == 3 and argv[1] == "pll":
        for r, f, a, b, v in pll_search(int(round(float(argv[2]) * 1e6))):
            print("REFDIV %d FBDIV %d POSTDIV1 %d POSTDIV2 %d VCO %.0f MHz" % (r, f, a, b, v / 1e6))
    elif argv[1:] == ["selftest"]:
        selftest()
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
