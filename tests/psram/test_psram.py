"""Host gate for the PicoCalc PSRAM driver.

Three layers, each against the shipped sources:

 1. The PIO program words in psram_pio.h are decoded and compared with the
    assembly in their comments, then executed by pio_sim.Pio against the
    ESP-PSRAM64H model in pio_sim.Device. Frames are driven directly, with
    an oracle written from the datasheet, and the waveform is checked:
    command, address and data nibbles on SIO0-3 at each SCK rising edge,
    pins released before the device drives them, and every Table 10-5
    timing the model enforces (tCSP, tCHD, tCPH, tCEM, clock period and duty, setup
    and hold).
 2. psram_xfer.c, compiled for the host, runs through the same model:
    reset, identification, quad entry, and reads and writes of arbitrary
    address, length and alignment against a shadow copy.
 3. Fault injection: wrong die status, wrong manufacturer, an absent
    part, a stuck data line, a processor too slow to keep the FIFOs fed,
    and pad delays beyond the sampling window must each be reported.

Usage: test_psram.py --lib LIB --header psram_pio.h [--xfer-header H]
Exit status 0 when every check passes.
"""

import argparse
import json
import random
import sys

import pio_sim
import rig as rigmod

CHECKS = 0
FAILS = []
WORDS = []


def check(cond, what):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILS.append(what)
        print("FAIL: " + what)
    return cond


def macro(text, name):
    import re

    m = re.search(r"#define\s+%s\s+\(?(-?\w+)\)?" % name, text)
    return int(m.group(1), 0)


class Consts:
    def __init__(self, lib, header_text, xfer_text):
        import ctypes

        for n in (
            "pinctrl",
            "pinctrl_csck",
            "execctrl",
            "shiftctrl",
            "clkdiv",
            "chunk",
        ):
            getattr(lib, "psram_host_" + n).restype = ctypes.c_uint
        lib.psram_host_ctrl_word.restype = ctypes.c_uint
        lib.psram_host_ctrl_word.argtypes = [ctypes.c_uint] * 3
        self.lib = lib
        self.pinctrl = lib.psram_host_pinctrl()
        self.pinctrl_csck = lib.psram_host_pinctrl_csck()
        self.execctrl = lib.psram_host_execctrl()
        self.shiftctrl = lib.psram_host_shiftctrl()
        self.clkdiv = lib.psram_host_clkdiv()
        self.chunk = lib.psram_host_chunk()
        self.length = macro(header_text, "PSRAM_PIO_LENGTH")
        self.e_timeout = macro(xfer_text, "PSRAM_E_TIMEOUT")
        self.e_stall = macro(xfer_text, "PSRAM_E_STALL")
        self.e_range = macro(xfer_text, "PSRAM_E_RANGE")
        self.e_id = macro(xfer_text, "PSRAM_E_ID")
        self.e_quad = macro(xfer_text, "PSRAM_E_QUAD")
        self.e_size = macro(xfer_text, "PSRAM_E_SIZE")

    def ctrl(self, nout, wait, nin):
        return self.lib.psram_host_ctrl_word(nout, wait, nin)


def ctrl_word(nout, wait, nin):
    """The control word from the layout in psram_pio.h, computed here."""
    return ((nout - 1) << 22) | ((wait + 1) << 17) | (nin << 7)


# -- layer 1: the program --------------------------------------------------


def test_static(words, c):
    check(
        len(words) == c.length,
        "PSRAM_PIO_LENGTH %d matches %d words" % (c.length, len(words)),
    )
    for i, (w, addr, asm) in enumerate(words):
        check(addr == i, "word %d is labeled %d" % (i, addr))
        try:
            d = pio_sim.disassemble(w)
        except ValueError as e:
            check(False, "word %d %04x does not decode: %s" % (i, w, e))
            continue
        check(
            pio_sim.norm(asm) == pio_sim.norm(d),
            "word %d %04x decodes to '%s' but its comment says '%s'"
            % (i, w, d, asm),
        )
    # Pin mapping computed from the board: SIO0-3 GP2-GP5, CS GP20 and SCK
    # GP21 as two side-set bits.
    want = (
        (2 << 29)
        | (4 << 26)
        | (4 << 20)
        | (2 << 15)
        | (20 << 10)
        | (2 << 5)
        | 2
    )
    check(c.pinctrl == want, "PINCTRL %#x != %#x" % (c.pinctrl, want))
    check(
        c.pinctrl_csck == ((2 << 26) | (20 << 5)),
        "CS/SCK setup PINCTRL %#x" % c.pinctrl_csck,
    )
    check(
        c.execctrl == (18 << 12), "EXECCTRL %#x wraps the program" % c.execctrl
    )
    check(
        c.shiftctrl == (3 << 16) and c.shiftctrl & (3 << 18) == 0,
        "SHIFTCTRL %#x: autopull and autopush at 32, shift left" % c.shiftctrl,
    )
    for nout, wait, nin in (
        (8, 6, 128),
        (2, 0, 0),
        (31, 1, 16),
        (136, 0, 0),
        (8, 0, 8),
    ):
        check(
            c.ctrl(nout, wait, nin) == ctrl_word(nout, wait, nin),
            "control word (%d,%d,%d)" % (nout, wait, nin),
        )


class Oracle:
    """Drives the state machine directly with frames the datasheet
    defines, without psram_xfer.c."""

    def __init__(self, words, c, **kw):
        self.c = c
        self.dev = pio_sim.Device(
            **{k: v for k, v in kw.items() if k in ("kgd", "mfid", "seed")}
        )
        self.pio = pio_sim.Pio(
            self.dev,
            out_ns=kw.get("out_ns", 3.0),
            in_ns=kw.get("in_ns", 3.0),
            access_ticks=kw.get("access_ticks", 6),
        )
        p = self.pio
        p.write(0x000, 0)
        for i, (w, _a, _s) in enumerate(words):
            p.write(0x048 + 4 * i, w)
        p.write(0x0C8, c.clkdiv << 16)
        p.write(0x0CC, c.execctrl)
        p.write(0x0D0, c.shiftctrl)
        p.write(0x0DC, c.pinctrl_csck)
        p.write(0x0D8, 0xE001)
        p.write(0x0D8, 0xE083)
        p.write(0x0DC, c.pinctrl)
        p.write(0x0D8, 0x0800)

    def frame(self, tx, nrx):
        p = self.pio
        rx = []
        i = 0
        while i < len(tx) and i < 4:
            p.write(0x010, tx[i])
            i += 1
        p.write(0x000, 1)
        for _ in range(4000):
            if i >= len(tx) and len(rx) >= nrx:
                break
            st = p.read(0x004)
            if i < len(tx) and not st & (1 << 16):
                p.write(0x010, tx[i])
                i += 1
            if len(rx) < nrx and not st & (1 << 8):
                rx.append(p.read(0x020))
        for _ in range(4000):
            if p.read(0x0D4) == 0 and p.read(0x004) & (1 << 24):
                break
        p.write(0x000, 0)
        return rx


def serial_word(b):
    w = 0
    for k in range(8):
        w = w << 4 | (b >> (7 - k)) & 1
    return w


def pack(data):
    out = []
    for i in range(0, len(data), 4):
        w = 0
        for j in range(4):
            w = w << 8 | (data[i + j] if i + j < len(data) else 0)
        out.append(w)
    return out


def nibbles(word_list, n):
    out = []
    for w in word_list:
        for k in range(8):
            out.append(w >> (28 - 4 * k) & 15)
    return out[:n]


def sample_nibbles(frame, lo, hi):
    return [
        s[0] | s[1] << 1 | s[2] << 2 | s[3] << 3
        for s in frame["samples"][lo:hi]
    ]


def test_waveforms(words, c):
    # Serial-mode read ID: the command as one bit per clock on SIO0, 23
    # don't-care address clocks with the pins then released, the ID read
    # back on SIO1.
    o = Oracle(words, c)
    tx = [ctrl_word(31, 1, 16), serial_word(0x9F), 0, 0, 0]
    rx = o.frame(tx, 2)
    f = o.dev.frames[-1]
    check(f["cmd"] == 0x9F, "serial read ID command decoded as %s" % f["cmd"])
    check(f["pulses"] == 31 + 3 + 16, "read ID clocks %d" % f["pulses"])
    want_bits = [(0x9F >> (7 - k)) & 1 for k in range(8)]
    got = [s[0] for s in f["samples"][:8]]
    check(got == want_bits, "command bits on SIO0 %s != %s" % (got, want_bits))
    check(
        all(s[1] == s[2] == s[3] == 0 for s in f["samples"][:31]),
        "serial command leaves SIO1-3 low",
    )
    t30 = f["rises"][30]
    check(
        all(o.dev.master_at(p, f["rises"][31])[1] == 0 for p in range(4)),
        "pins released before the 32nd clock (t=%.1f)" % t30,
    )
    bits = [(nibbles(rx, 16)[k] >> 1) & 1 for k in range(16)]
    ident = 0
    for b in bits[:8]:
        ident = ident << 1 | b
    kgd = 0
    for b in bits[8:]:
        kgd = kgd << 1 | b
    check((ident, kgd) == (0x0D, 0x5D), "ID read %02x/%02x" % (ident, kgd))
    check(not o.dev.violations, "read ID violations: %s" % o.dev.violations)

    # Quad-mode write 0x38: two command nibbles, six address nibbles, the
    # data nibbles high first.
    o = Oracle(words, c)
    o.dev.qpi = True
    data = bytes([0xA5, 0x3C, 0x00, 0xFF, 0x12])
    addr = 0x001234
    tx = [ctrl_word(8 + 2 * len(data), 0, 0), 0x38 << 24 | addr] + pack(data)
    o.frame(tx, 0)
    f = o.dev.frames[-1]
    want = [3, 8, 0, 0, 1, 2, 3, 4]
    for b in data:
        want += [b >> 4, b & 15]
    check(
        sample_nibbles(f, 0, len(want)) == want,
        "write nibbles %s != %s" % (sample_nibbles(f, 0, len(want)), want),
    )
    check(
        f["pulses"] == len(want),
        "write clocks %d != %d" % (f["pulses"], len(want)),
    )
    stored = bytes(o.dev.mem[addr : addr + len(data)])  # noqa: E203
    check(
        stored == data,
        "write stored %s" % stored.hex(),
    )
    check(not o.dev.violations, "write violations: %s" % o.dev.violations)

    # Quad-mode fast read 0xEB: command and address, six clocks with the
    # pins released, then data; the state machine samples one clock after
    # the nibble's own rising edge, which is why the frame is longer than
    # the datasheet's by one clock of turnaround.
    o = Oracle(words, c)
    o.dev.qpi = True
    addr = 0x000400
    want_data = bytes(o.dev.mem[addr : addr + 8])  # noqa: E203
    tx = [ctrl_word(8, 6, 16), 0xEB << 24 | addr]
    rx = o.frame(tx, 2)
    f = o.dev.frames[-1]
    check(
        sample_nibbles(f, 0, 8) == [14, 11, 0, 0, 0, 4, 0, 0],
        "read command and address nibbles %s" % sample_nibbles(f, 0, 8),
    )
    check(f["pulses"] == 8 + 6 + 2 + 16, "read clocks %d" % f["pulses"])
    for j in range(8, f["pulses"]):
        rel = o.dev.master_at(0, f["rises"][j])[1]
        check(rel == 0, "SIO0 still driven at clock %d" % j)
    got = bytes(w >> s & 255 for w in rx for s in (24, 16, 8, 0))
    check(
        got == want_data, "read data %s != %s" % (got.hex(), want_data.hex())
    )
    check(not o.dev.violations, "read violations: %s" % o.dev.violations)
    check(
        o.dev.poison == 0, "read sampled %d indeterminate bits" % o.dev.poison
    )
    return o


def test_frame_limits(words, c):
    """The longest frames: tCEM, framing and clock rules, margins."""
    o = Oracle(words, c)
    o.dev.qpi = True
    n = c.chunk
    data = bytes(random.Random(3).getrandbits(8) for _ in range(n))
    o.frame([ctrl_word(8 + 2 * n, 0, 0), 0x38 << 24 | 0x400] + pack(data), 0)
    wlow = o.dev.frames[-1]["t_rise"] - o.dev.frames[-1]["t_fall"]
    o.frame([ctrl_word(8, 6, 2 * n), 0xEB << 24 | 0x400], n // 4)
    rlow = o.dev.frames[-1]["t_rise"] - o.dev.frames[-1]["t_fall"]
    check(
        max(wlow, rlow) <= pio_sim.T_CEM,
        "longest frame holds CS low %.0f ns, limit %.0f"
        % (max(wlow, rlow), pio_sim.T_CEM),
    )
    check(
        not o.dev.violations, "frame-limit violations: %s" % o.dev.violations
    )
    d = o.dev
    check(d.min_tcsp >= pio_sim.T_CSP, "tCSP %.1f" % d.min_tcsp)
    check(d.min_tchd >= pio_sim.T_CHD, "tCHD %.1f" % d.min_tchd)
    check(d.min_tcph >= pio_sim.T_CPH, "tCPH %.1f" % d.min_tcph)
    check(
        d.min_period >= pio_sim.T_CLK_MIN, "clock period %.1f" % d.min_period
    )
    return {
        "write_cs_low_ns": wlow,
        "read_cs_low_ns": rlow,
        "tCSP_ns": d.min_tcsp,
        "tCHD_ns": d.min_tchd,
        "tCPH_ns": d.min_tcph,
        "sck_period_ns": d.min_period,
        "read_margin_start_ns": min(m[0] for m in d.margins),
        "read_margin_end_ns": min(m[1] for m in d.margins if m[1] is not None),
    }


# -- layer 2: the driver -----------------------------------------------------


def new_rig(lib_path, **kw):
    dkw = {
        k: kw.pop(k)
        for k in list(kw)
        if k in ("kgd", "mfid", "stuck_low_sio", "absent", "seed", "eid", "size")
    }
    return rigmod.Rig(lib_path, device=pio_sim.Device(**dkw), **kw)


def test_chunks(lib):
    rnd = random.Random(5)
    ok = True
    for _ in range(2000):
        addr = rnd.randrange(0, 1 << 23)
        ln = rnd.randrange(1, 4000)
        n = lib.psram_chunk_len(addr, ln)
        ok &= 0 < n <= min(ln, 64)
        ok &= (addr // 64) == ((addr + n - 1) // 64)
        ok &= (addr // 1024) == ((addr + n - 1) // 1024)
    check(ok, "frames stay inside one 64-byte block and one 1 KB page")
    covered, a = 0, 1000
    while covered < 3000:
        covered += lib.psram_chunk_len(a + covered, 3000 - covered)
    check(covered == 3000, "chunks cover the request exactly")


def test_duty_rule():
    """The 45%-55% band is applied to each phase against the cycle's own
    period. The 32 ns cycle with an 8 ns high phase, which a check against
    the 7 ns minimum period accepts, must be rejected, in either
    orientation."""
    for high, low, ok in (
        (16.0, 16.0, True),
        (14.4, 17.6, True),
        (17.6, 14.4, True),
        (14.0, 18.0, False),
        (18.0, 14.0, False),
        (8.0, 24.0, False),
        (24.0, 8.0, False),
        (16.0, 32.0, False),
        (3.2, 3.8, True),
    ):
        got = pio_sim.duty_problem(high, low) is None
        check(
            got == ok,
            "duty rule: %.1f ns high / %.1f ns low is %s, want %s"
            % (high, low, "accepted" if got else "rejected",
               "accepted" if ok else "rejected"),
        )


def test_block_offset(lib):
    """psramstrategy() turns b_blkno into a byte offset through
    psram_block_offset(); a block number outside the device must be refused
    before the multiplication can wrap the 32-bit offset onto address 0."""
    import ctypes

    fn = lib.psram_block_offset
    fn.restype = ctypes.c_int
    fn.argtypes = [
        ctypes.c_long,
        ctypes.c_uint,
        ctypes.c_uint,
        ctypes.POINTER(ctypes.c_uint),
    ]
    bsize = 1024
    blocks = (8 * 1024 * 1024) // bsize

    def call(blk, ln):
        off = ctypes.c_uint(0xDEADBEEF)
        return fn(blk, bsize, ln, ctypes.byref(off)), off.value

    for blk in (1 << 22, -(1 << 22), 1 << 31, -(1 << 31), (1 << 22) + 1, -1):
        e, off = call(blk, bsize)
        check(
            e == -3 and off == 0xDEADBEEF,
            "block %d is refused without an offset (%d, %#x)" % (blk, e, off),
        )
    for blk, ln in ((blocks + 1, 0), (blocks, 1), (blocks - 1, bsize + 1)):
        e, _ = call(blk, ln)
        check(e == -3, "block %d length %d runs past the end" % (blk, ln))
    for blk, ln in (
        (0, bsize),
        (7, 3),
        (blocks - 1, bsize),
        (blocks, 0),
        (blocks - 1, 1),
    ):
        e, off = call(blk, ln)
        check(
            e == 0 and off == blk * bsize,
            "block %d length %d maps to offset %d (%d, %d)"
            % (blk, ln, blk * bsize, e, off),
        )


def test_driver(lib_path, c, access_ticks=6, quiet=False, **rig_kw):
    r = new_rig(lib_path, access_ticks=access_ticks, **rig_kw)
    r.hw_init()
    check(
        sorted(p for p, _ in r.pins) == [2, 3, 4, 5, 20, 21],
        "pins routed: %s" % sorted(p for p, _ in r.pins),
    )
    check(
        r.pio.imem[: c.length] == [w for w, _a, _s in WORDS],
        "program words loaded into instruction memory",
    )
    import ctypes

    pads = dict(r.pins)
    # PADS_BANK0 GPIOn (datasheet 2.19.6.3): reset 0x56, DRIVE bits 5:4 hold
    # the strength, not a mask to OR into. 8 mA is 0b10, so the register
    # must read IE|DRIVE=2|SCHMITT|SLEWFAST = 0x63, plus PDE on the four
    # data pins = 0x67; an OR into the reset field would read 0x73 / 0x77.
    want = {2: 0x67, 3: 0x67, 4: 0x67, 5: 0x67, 20: 0x63, 21: 0x63}
    check(
        pads == want,
        "pad words %s want %s" % ({k: hex(v) for k, v in sorted(pads.items())},
                                  {k: hex(v) for k, v in sorted(want.items())}),
    )
    check(
        all((v >> 4) & 3 == 2 for v in pads.values()),
        "every PSRAM pad selects 8 mA",
    )
    # A pad word that names no drive strength leaves the reset 4 mA alone.
    r.lib.psram_host_unreset()
    r.lib.psram_host_pin(10, 0x8 | 0x2)
    r.lib.psram_host_pad_word.restype = ctypes.c_uint
    check(
        r.lib.psram_host_pad_word(10) == 0x5A,
        "a pad request without a drive field keeps 4 mA: %#x"
        % r.lib.psram_host_pad_word(10),
    )
    err = r.device_init()
    check(err == 0, "device init returned %d" % err)
    check(r.ids() == (0x0D, 0x5D), "ids %s" % (r.ids(),))
    check(
        r.eid() == (bytes(r.dev.eid), 1),
        "EID %s read %d, want %s" % (r.eid()[0].hex(), r.eid()[1], r.dev.eid.hex()),
    )
    check(r.dev.qpi, "device is in quad mode after init")
    seq = [f["cmd"] for f in r.dev.frames]
    check(
        seq[:4] == [None, None, 0x66, 0x99]
        or seq[:4] == [0x66, 0x99, 0x66, 0x99],
        "reset pair sequence %s" % seq[:4],
    )
    rnd = random.Random(11)
    shadow = {}
    for _ in range(40):
        base = rnd.choice([0, 0x3F0, 0x400, 0x10000, 0x7FFF00, 0x123457])
        addr = base + rnd.randrange(0, 70)
        ln = rnd.choice([1, 2, 3, 4, 5, 63, 64, 65, 100, 128, 200, 301])
        if addr + ln > 1 << 23:
            addr = (1 << 23) - ln
        data = bytes(rnd.getrandbits(8) for _ in range(ln))
        e = r.write(addr, data)
        if not check(e == 0, "write %#x+%d -> %d" % (addr, ln, e)):
            return r
        for k in range(ln):
            shadow[addr + k] = data[k]
        e, back = r.read(addr, ln)
        if not check(
            e == 0 and back == data,
            "read-back %#x+%d err %d match %s" % (addr, ln, e, back == data),
        ):
            return r
    for a, b in shadow.items():
        if r.dev.mem[a] != b:
            check(
                False,
                "memory %#x holds %02x, want %02x" % (a, r.dev.mem[a], b),
            )
            break
    else:
        check(True, "device memory equals the shadow copy")
    e, _ = r.read((1 << 23) - 4, 5)
    check(e == c.e_range, "read past the end -> %d" % e)
    e = r.write((1 << 23) - 4, b"12345")
    check(e == c.e_range, "write past the end -> %d" % e)
    e, tail = r.read((1 << 23) - 4, 4)
    check(e == 0 and tail == bytes(r.dev.mem[-4:]), "read of the last 4 bytes")
    e, empty = r.read(100, 0)
    check(e == 0 and empty == b"", "zero-length read")
    if not quiet:
        check(
            not r.dev.violations,
            "driver violations: %s" % r.dev.violations[:5],
        )
        check(
            r.dev.poison == 0,
            "driver sampled %d indeterminate bits" % r.dev.poison,
        )
        check(r.pio.irq_depth == 0, "interrupt mask balanced")
        check(
            max(r.pio.irq_spans) <= pio_sim.T_CEM,
            "interrupts masked %.0f ns at most" % max(r.pio.irq_spans),
        )
    return r


def test_faults(lib_path, c):
    cases = [
        ("known-good-die fail", dict(kgd=0x55), c.e_id),
        ("wrong manufacturer", dict(mfid=0x9D), c.e_id),
        ("absent part", dict(absent=True), c.e_id),
        ("stuck SIO2", dict(stuck_low_sio=2), c.e_quad),
    ]
    for name, kw, want in cases:
        r = new_rig(lib_path, **kw)
        r.hw_init()
        e = r.device_init()
        check(e == want, "%s: init returned %d, want %d" % (name, e, want))
        check(
            not r.dev.violations,
            "%s: violations %s" % (name, r.dev.violations[:3]),
        )


def test_eid(lib_path, c):
    """Read ID returns 48 EID bits after MF and KGD; each byte position
    and bit order must reach psram_eid, on a passing part and on parts the
    probe refuses, and an RX overrun during Read ID repeats the reset."""
    aps_like = bytes([0x5A, 0x01, 0x80, 0xFF, 0x00, 0x7E])
    for name, kw, want_err in (
        ("APS-style density 010", dict(eid=aps_like), 0),
        ("known-good-die fail keeps its EID", dict(eid=aps_like, kgd=0x55), c.e_id),
        ("absent part reads zeros", dict(absent=True), c.e_id),
    ):
        r = new_rig(lib_path, **kw)
        r.hw_init()
        e = r.device_init()
        want = bytes(6) if kw.get("absent") else aps_like
        check(e == want_err, "%s: init %d, want %d" % (name, e, want_err))
        check(
            r.eid() == (want, 1),
            "%s: EID %s read %d" % (name, r.eid()[0].hex(), r.eid()[1]),
        )
    # The full Read ID runs 31 clocks out, three with the pins released and
    # 64 in, and holds CS low inside tCEM.
    r = new_rig(lib_path)
    r.hw_init()
    r.device_init()
    f = r.dev.frames[4]
    check(
        f["cmd"] == 0x9F and f["pulses"] == 31 + 3 + 64,
        "full Read ID frame: command %s, %d clocks" % (f["cmd"], f["pulses"]),
    )
    low = f["t_rise"] - f["t_fall"]
    check(low < pio_sim.T_CEM, "full Read ID holds CS low %.0f ns" % low)
    # Frames 0-3 are the two reset pairs; frame 4 is the first Read ID.
    r = new_rig(lib_path, forced_stalls=(4,))
    r.hw_init()
    e = r.device_init()
    seq = [f["cmd"] for f in r.dev.frames]
    check(e == 0, "an overrun Read ID is retried: init %d" % e)
    check(
        seq[4] == 0x9F and seq[5:9] in ([None, None, 0x66, 0x99], [0x66, 0x99, 0x66, 0x99])
        and seq[9] == 0x9F,
        "the retry resets the part before Read ID again: %s" % seq[:10],
    )
    check(r.eid() == (bytes(r.dev.eid), 1), "the retried Read ID decodes the EID")
    r = new_rig(lib_path, forced_stalls=(4, 9, 14))
    r.hw_init()
    e = r.device_init()
    check(
        e == c.e_stall and r.eid()[1] == 0,
        "three overrun Read IDs give up with %d (read %d)" % (e, r.eid()[1]),
    )


def test_extent(lib_path, c):
    """A part that answers 0d/5d with fewer than 23 address bits wraps its
    addresses; the probe must refuse it before swap does, and accept the
    full 8 MB part."""
    for size in (1 << 10, 1 << 16, 1 << 20, 2 << 20, 4 << 20, 8 << 20):
        r = new_rig(lib_path, size=size)
        r.hw_init()
        e = r.device_init()
        want = 0 if size == pio_sim.SIZE else c.e_size
        check(e == want, "a %d-byte part: init %d, want %d" % (size, e, want))


def test_cpu_speed(lib_path, c):
    for ticks in (2, 3, 4, 6, 8, 12):
        r = test_driver(lib_path, c, access_ticks=ticks, quiet=True)
        check(
            not r.dev.violations,
            "access cost %d ticks: %s" % (ticks, r.dev.violations[:3]),
        )
        check(r.dev.poison == 0, "access cost %d ticks poisoned" % ticks)
    # A processor that takes microseconds per register access cannot keep
    # the FIFOs fed; the gate must see it.
    r = new_rig(lib_path, access_ticks=400)
    r.hw_init()
    e = r.device_init()
    seen = e != 0 or bool(r.dev.violations)
    if not seen:
        w = r.write(0x100, bytes(range(64)))
        e2, back = r.read(0x100, 64)
        seen = w != 0 or e2 != 0 or bool(r.dev.violations)
    check(seen, "a 3.2 us register access cost goes unnoticed")


def test_stage_gating(lib_path, c):
    """The datasheet does not say whether the output register stage and the
    input register stage advance on clk_sys or on the state machine's clock
    enable (3.5.6.1). The driver must read correctly under all four
    placements; the table records the sampling margin of each."""
    table = {}
    for out_gated in (False, True):
        for in_gated in (False, True):
            r = test_driver(
                lib_path,
                c,
                quiet=True,
                out_gated=out_gated,
                in_gated=in_gated,
            )
            name = "out_%s_in_%s" % (
                "enable" if out_gated else "sys",
                "enable" if in_gated else "sys",
            )
            start = min(m[0] for m in r.dev.margins)
            end = min(m[1] for m in r.dev.margins if m[1] is not None)
            check(
                not r.dev.violations,
                "%s: %s" % (name, r.dev.violations[:3]),
            )
            check(
                r.dev.poison == 0,
                "%s: %d indeterminate samples" % (name, r.dev.poison),
            )
            check(
                start >= 2.0 and end >= 2.0,
                "%s: read margins %.1f / %.1f ns" % (name, start, end),
            )
            table[name] = {
                "read_margin_start_ns": start,
                "read_margin_end_ns": end,
            }
    return table


def test_pad_delay(lib_path, c):
    """The read window is wide enough for any combined output and input
    delay s with 0 <= s <= 2*cycle - tACLK; a larger delay must show."""
    cyc = c.clkdiv * pio_sim.SYS_NS
    for out_ns, in_ns in ((0, 0), (1, 1), (3, 3), (4, 6), (2, 8), (5, 5)):
        r = new_rig(lib_path, out_ns=float(out_ns), in_ns=float(in_ns))
        r.hw_init()
        e = r.device_init()
        check(
            e == 0 and not r.dev.violations and r.dev.poison == 0,
            "pad delays out %s in %s ns: init %d poison %d"
            % (out_ns, in_ns, e, r.dev.poison),
        )
    big = 2 * cyc - pio_sim.T_ACLK_MAX + 4
    r = new_rig(lib_path, out_ns=big / 2, in_ns=big / 2)
    r.hw_init()
    e = r.device_init()
    check(
        e != 0 or r.dev.poison > 0,
        "a %.0f ns pad delay passes the read window" % big,
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", required=True)
    ap.add_argument("--header", required=True)
    ap.add_argument("--xfer-header", required=True)
    ap.add_argument("--json")
    a = ap.parse_args()
    text = open(a.header).read()
    words = pio_sim.parse_words(text)
    WORDS.extend(words)
    import ctypes

    lib = ctypes.CDLL(a.lib)
    c = Consts(lib, text, open(a.xfer_header).read())
    results = {}
    test_static(words, c)
    test_waveforms(words, c)
    results["frame_limits"] = test_frame_limits(words, c)
    test_chunks(lib)
    test_duty_rule()
    test_block_offset(lib)
    r = test_driver(a.lib, c)
    results["driver"] = {
        "frames": len(r.dev.frames),
        "irq_masked_max_ns": max(r.pio.irq_spans),
        "register_accesses": r.pio.reg_accesses,
        "read_margin_start_ns": min(m[0] for m in r.dev.margins),
        "read_margin_end_ns": min(
            m[1] for m in r.dev.margins if m[1] is not None
        ),
    }
    test_faults(a.lib, c)
    test_eid(a.lib, c)
    test_extent(a.lib, c)
    test_cpu_speed(a.lib, c)
    test_pad_delay(a.lib, c)
    results["stage_gating"] = test_stage_gating(a.lib, c)
    print("psram: %d checks, %d failed" % (CHECKS, len(FAILS)))
    if a.json:
        json.dump(results, open(a.json, "w"), indent=2, sort_keys=True)
    if FAILS:
        sys.exit(1)
    print(json.dumps(results, sort_keys=True))


if __name__ == "__main__":
    main()
