"""Instruction-level simulator of one RP2040 PIO state machine and a timing
model of the ESP-PSRAM64H, written from the RP2040 datasheet chapter 3 and
the Espressif ESP-PSRAM64/ESP-PSRAM64H datasheet v1.1 (docs/research/
picocalc-psram.md lists the pages).

Only the instructions psram_pio.h uses are executed: JMP, IN, OUT, PULL,
MOV, SET and the side-set field. Anything else raises, so a program that
grows an instruction the model lacks fails loudly instead of passing.

Time is in nanoseconds. A state machine cycle lasts clkdiv * 8 ns
(clk_sys is 125 MHz) and an instruction executes at the start of its cycle.
The two input synchronizer flip-flops are timed against CLK_SYS (datasheet
3.5.6.3, the warning on bypassing them), and the clock divider only gates
state machine execution (3.5.5), so the synchronizers advance every clk_sys
cycle whatever the divider. The datasheet also counts a register stage
between the state machine and the pin mapping and one between the input
mapping and the state machine, four cycles in all for a state machine that
observes its own pad (3.5.6.1), without naming their clock. The model times
both on clk_sys: an instruction executed in cycle k changes the pad
OUT_STAGES clk_sys cycles after its cycle starts and reaches the device
out_ns later, and an IN executed in cycle k sees the pad level IN_STAGES
clk_sys cycles before its cycle starts, with in_ns modeling the device pin
to input flip-flop delay. Pio(out_gated=True) and Pio(in_gated=True) move
one stage onto the state machine cycle for the sensitivity run in
test_stage_gating(). out_ns and in_ns are board and pad properties the
datasheets do not give; tests sweep them.
"""

import bisect
import random
import re

SYS_NS = 8.0
OUT_STAGES = 1  # output register stage, clk_sys cycles
IN_STAGES = 3  # two synchronizer flip-flops plus the input register stage

# Datasheet Table 10-5 (page 17-18 of v1.1), nanoseconds.
T_CLK_MIN = 7.0  # 133 MHz
T_CLK_PAGE = 1000.0 / 84.0  # 84 MHz when a burst crosses a 1 KB page
T_CH_MIN = 0.45 * T_CLK_MIN
# tCH and tCL, each 0.45 to 0.55 of the clock period (Table 10-5).
DUTY_MIN = 0.45
DUTY_MAX = 0.55
# SCK clocks the master drives from the output loop before the first
# turnaround or data clock of a read: quad 0xEB is 2 command and 6 address
# nibbles; serial Read ID 0x9F is 8 command and 24 address bits, of which
# the last is clocked by the turnaround loop because the driver releases
# the pins one clock early (PSRAM_ID_WAIT). The state machine leaves the
# output loop, releases the pins and loads the input counts with SCK low, so
# the cycle that ends at the clock after this many is stretched by those
# instructions. It is the only cycle of a frame the program may stretch.
OUT_CLOCKS = {"qread": 8, "rdid": 31}


def duty_problem(high, low):
    """Describe how a clock cycle of the given high and low times (ns)
    leaves the 45%-55% band of its own period, or return None."""
    period = high + low
    for name, part in (("high", high), ("low", low)):
        frac = part / period
        if not DUTY_MIN - 1e-9 <= frac <= DUTY_MAX + 1e-9:
            return "SCK %s %.1f ns of a %.1f ns cycle is %.0f%%" % (
                name,
                part,
                period,
                100 * frac,
            )
    return None

T_CPH = 50.0
T_CEM = 8000.0
T_CSP = 2.5
T_CHD = 20.0
T_SP = 2.0
T_HD = 2.0
T_ACLK_MIN = 2.0
T_ACLK_MAX = 6.0
T_KOH = 1.5
T_HZ = 6.0
PAGE = 1024
SIZE = 8 * 1024 * 1024

SIDE_BITS = 2
DELAY_BITS = 5 - SIDE_BITS
SIDE_BASE = 20
OUT_BASE = 2

WORD_RE = re.compile(r"0x([0-9a-fA-F]{4}),?\s*/\*\s*(\d+):\s*(.*?)\s*\*/")


def parse_words(text):
    """Return [(word, address, assembly)] from PSRAM_PIO_WORDS."""
    m = re.search(r"#define\s+PSRAM_PIO_WORDS\s*\\\n(.*?)\n\n", text, re.S)
    if m is None:
        raise ValueError("PSRAM_PIO_WORDS not found")
    out = []
    for line in m.group(1).split("\n"):
        w = WORD_RE.search(line)
        if w is None:
            raise ValueError("unparsable PIO word line: %r" % line)
        out.append((int(w.group(1), 16), int(w.group(2)), w.group(3)))
    return out


def define_value(text, name):
    m = re.search(r"#define\s+%s\s+(\S+)" % name, text)
    if m is None:
        raise ValueError(name + " not found")
    return int(m.group(1), 0)


JMP_COND = ["", "!x", "x--", "!y", "y--", "x!=y", "pin", "!osre"]
OUT_DEST = ["pins", "x", "y", "null", "pindirs", "pc", "isr", "exec"]
MOV_DEST = ["pins", "x", "y", "?", "exec", "pc", "isr", "osr"]
MOV_SRC = ["pins", "x", "y", "null", "?", "status", "isr", "osr"]
IN_SRC = ["pins", "x", "y", "null", "?", "?", "isr", "osr"]
SET_DEST = ["pins", "x", "y", "?", "pindirs", "?", "?", "?"]


def decode(word, side_bits=SIDE_BITS):
    """Split a word into fields; side_bits is PINCTRL.SIDESET_COUNT."""
    op = word >> 13
    ds = (word >> 8) & 0x1F
    delay_bits = 5 - side_bits
    return {
        "op": op,
        "side": ds >> delay_bits,
        "side_bits": side_bits,
        "delay": ds & ((1 << delay_bits) - 1),
        "arg": word & 0xFF,
    }


def disassemble(word):
    """Assembly text in the form psram_pio.h comments use."""
    f = decode(word)
    op, arg = f["op"], f["arg"]
    hi, lo = arg >> 5, arg & 0x1F
    if op == 0:
        cond = JMP_COND[hi]
        text = "jmp %s%d" % (cond + ", " if cond else "", lo)
    elif op == 1:
        raise ValueError("WAIT is not modeled")
    elif op == 2:
        text = "in %s, %d" % (IN_SRC[hi], lo or 32)
    elif op == 3:
        text = "out %s, %d" % (OUT_DEST[hi], lo or 32)
    elif op == 4:
        if arg & 0x80:
            if arg & 0x40:
                raise ValueError("pull ifempty is not modeled")
            text = "pull block" if arg & 0x20 else "pull noblock"
        else:
            raise ValueError("push is not modeled")
    elif op == 5:
        if (arg >> 3) & 3:
            raise ValueError("mov operation not modeled")
        if (hi, arg & 7) == (2, 2):
            text = "nop"
        else:
            text = "mov %s, %s" % (MOV_DEST[hi], MOV_SRC[arg & 7])
    elif op == 6:
        raise ValueError("IRQ is not modeled")
    else:
        text = "set %s, %d" % (SET_DEST[hi], lo)
    text += " side %d" % f["side"]
    if f["delay"]:
        text += " [%d]" % f["delay"]
    return text


def norm(text):
    return " ".join(text.split())


class Device:
    """ESP-PSRAM64H at the pins: checks Table 10-5 and executes the
    commands the driver uses, in serial and quad mode."""

    def __init__(
        self, kgd=0x5D, mfid=0x0D, stuck_low_sio=None, absent=False, seed=1
    ):
        rnd = random.Random(seed)
        self.mem = bytearray(rnd.getrandbits(8) for _ in range(4096)) * (
            SIZE // 4096
        )
        self.eid = bytes([0xC0, 0xDE, 0x12, 0x34, 0x56, 0x78])
        self.kgd, self.mfid = kgd, mfid
        self.stuck_low_sio = stuck_low_sio
        self.absent = absent
        self.qpi = False
        self.rst_en = False
        self.violations = []
        self.frames = []  # one dict per chip-select frame
        self.hist = [[(-1e18, 0, 0)] for _ in range(4)]
        self.cs = 1
        self.sck = 0
        self.cs_fall = None
        self.last_cs_rise = None
        self.last_rise = None
        self.last_fall = None
        self.last_sio_change = -1e18
        self.margins = []  # (start, end) per sampled data window
        self.poison = 0
        self.max_cs_low = 0.0
        self.min_tcsp = self.min_tchd = self.min_tcph = 1e18
        self.min_period = 1e18
        self.crossed_page = False
        self.frame = None

    # -- violations ---------------------------------------------------
    def flag(self, t, what):
        self.violations.append("%.1f ns: %s" % (t, what))

    # -- master side of the pins --------------------------------------
    def _resolved(self, lvl, dr):
        return [(lvl >> i) & 1 if (dr >> i) & 1 else 0 for i in range(4)]

    def master_at(self, pin, t):
        h = self.hist[pin]
        i = bisect.bisect_right(h, (t, 9, 9)) - 1
        return h[i][1], h[i][2]

    def pad_event(self, t, cs, sck, lvl, dr):
        """The pad levels the state machine drives, as the device sees
        them at time t."""
        before = self._resolved(*self._master_now())
        for pin in range(4):
            level, dirn = (lvl >> pin) & 1, (dr >> pin) & 1
            if (level, dirn) != self.hist[pin][-1][1:]:
                self.hist[pin].append((t, level, dirn))
        after = self._resolved(lvl, dr)
        if after != before:
            self.last_sio_change = t
            if (
                self.frame is not None
                and self.last_rise is not None
                and 0 <= t - self.last_rise < T_HD
            ):
                self.flag(
                    t,
                    "data changed %.1f ns after SCK rise "
                    "(hold %.1f)" % (t - self.last_rise, T_HD),
                )
        fr = self.frame
        if (
            fr is not None
            and fr.get("driving")
            and fr["drive_from"] is not None
        ):
            for pin in fr["driving"]:
                if (dr >> pin) & 1 and t >= fr["drive_from"]:
                    self.flag(t, "contention on SIO%d" % pin)
        if cs != self.cs:
            (self._cs_rise if cs else self._cs_fall)(t)
        if sck != self.sck:
            (self._sck_rise if sck else self._sck_fall)(t)

    def _master_now(self):
        return (
            sum(h[-1][1] << i for i, h in enumerate(self.hist)),
            sum(h[-1][2] << i for i, h in enumerate(self.hist)),
        )

    # -- device output -------------------------------------------------
    def sio_value(self, pin, t):
        """What an input flip-flop sees on SIO<pin> at device time t."""
        level, dirn = self.master_at(pin, t)
        if dirn:
            return level
        fr = self.frame
        if (
            fr is not None
            and fr.get("driving")
            and pin in fr["driving"]
            and fr["drive_from"] is not None
            and t >= fr["drive_from"]
        ):
            wins = fr["wins"]
            starts = [w[0] for w in wins]
            i = bisect.bisect_right(starts, t) - 1
            if i < 0 or (wins[i][1] is not None and t > wins[i][1]):
                self.poison += 1
                return fr["value"](None, pin)
            self.margins.append(
                (
                    t - wins[i][0],
                    None if wins[i][1] is None else wins[i][1] - t,
                    i,
                )
            )
            return fr["value"](i, pin)
        return 0

    # -- edges -----------------------------------------------------------
    def _cs_fall(self, t):
        self.cs = 0
        if self.last_cs_rise is not None:
            self.min_tcph = min(self.min_tcph, t - self.last_cs_rise)
        if self.last_cs_rise is not None and t - self.last_cs_rise < T_CPH:
            self.flag(
                t,
                "CS high only %.1f ns between frames (tCPH %.0f)"
                % (t - self.last_cs_rise, T_CPH),
            )
        self.cs_fall = t
        self.last_rise = self.last_fall = None
        self.crossed_page = False
        self.frame = {
            "pulses": 0,
            "samples": [],
            "bits": [],
            "cmd": None,
            "addr": None,
            "t_fall": t,
            "rises": [],
            "falls": [],
            "pauses": 0,
            "driving": None,
            "wins": [],
            "drive_from": None,
            "p0": None,
            "wbytes": 0,
            "t_rise_last": None,
        }

    def _cs_rise(self, t):
        self.cs = 1
        fr = self.frame
        if fr is None:
            return
        low = t - self.cs_fall
        self.max_cs_low = max(self.max_cs_low, low)
        if low > T_CEM:
            self.flag(t, "CS low %.0f ns exceeds tCEM %.0f" % (low, T_CEM))
        if fr["t_rise_last"] is not None:
            self.min_tchd = min(self.min_tchd, t - fr["t_rise_last"])
        if fr["t_rise_last"] is not None and t - fr["t_rise_last"] < T_CHD:
            self.flag(
                t,
                "CS rose %.1f ns after the last SCK rise (tCHD %.0f)"
                % (t - fr["t_rise_last"], T_CHD),
            )
        self._finish_frame(fr, t)
        self.last_cs_rise = t
        self.frame = None

    def _sck_rise(self, t):
        self.sck = 1
        fr = self.frame
        if fr is None:
            self.flag(t, "SCK rose with CS high")
            return
        if t - self.last_sio_change < T_SP:
            self.flag(
                t,
                "data changed %.1f ns before SCK rise (setup %.1f)"
                % (t - self.last_sio_change, T_SP),
            )
        if fr["pulses"] == 0:
            self.min_tcsp = min(self.min_tcsp, t - fr["t_fall"])
        if fr["pulses"] == 0 and t - fr["t_fall"] < T_CSP:
            self.flag(
                t,
                "first SCK rise %.1f ns after CS fall (tCSP %.1f)"
                % (t - fr["t_fall"], T_CSP),
            )
        if self.last_rise is not None:
            period = t - self.last_rise
            self.min_period = min(self.min_period, period)
            limit = T_CLK_PAGE if self.crossed_page else T_CLK_MIN
            if period < limit - 1e-9:
                self.flag(t, "SCK period %.1f ns below %.1f" % (period, limit))
        if self.last_fall is not None and t - self.last_fall < T_CH_MIN:
            self.flag(t, "SCK low %.1f ns" % (t - self.last_fall))
        if self.last_rise is not None and self.last_fall is not None:
            # The one stretched cycle per read, between the output loop and
            # the turnaround, is the only place the program pauses SCK.
            pause = OUT_CLOCKS.get(fr.get("kind")) == fr["pulses"]
            problem = duty_problem(
                self.last_fall - self.last_rise, t - self.last_fall
            )
            if problem is not None and not pause:
                self.flag(t, problem)
            if problem is not None and pause:
                fr["pauses"] += 1
        self.last_rise = t
        fr["t_rise_last"] = t
        fr["rises"].append(t)
        # What the device latches is what the master drives; a pin the
        # master has released reads as pulled down.
        sample = []
        for p in range(4):
            level, dirn = self.master_at(p, t)
            sample.append(level if dirn else 0)
        fr["samples"].append(sample)
        self._consume(fr, fr["pulses"], sample, t)
        fr["pulses"] += 1

    def _sck_fall(self, t):
        self.sck = 0
        fr = self.frame
        if fr is None:
            return
        if self.last_rise is not None and t - self.last_rise < T_CH_MIN:
            self.flag(t, "SCK high %.1f ns" % (t - self.last_rise))
        self.last_fall = t
        j = fr["pulses"] - 1
        fr["falls"].append(t)
        if fr["p0"] is not None and j >= fr["p0"] - 1:
            k = j - (fr["p0"] - 1)
            if fr["wins"]:
                fr["wins"][-1][1] = t + T_KOH
            fr["wins"].append([t + T_ACLK_MAX, None, k])
            if fr["drive_from"] is None:
                fr["drive_from"] = t + T_ACLK_MIN

    # -- command decode ----------------------------------------------------
    def _consume(self, fr, j, sample, t):
        qpi = fr.setdefault("qpi", self.qpi)
        nib = sample[0] | sample[1] << 1 | sample[2] << 2 | sample[3] << 3
        if fr["cmd"] is None:
            if qpi:
                fr["bits"].append(nib)
                if len(fr["bits"]) == 2:
                    fr["cmd"] = fr["bits"][0] << 4 | fr["bits"][1]
                    fr["bits"] = []
                    self._cmd_decoded(fr, qpi)
            else:
                fr["bits"].append(sample[0])
                if len(fr["bits"]) == 8:
                    c = 0
                    for b in fr["bits"]:
                        c = c << 1 | b
                    fr["cmd"] = c
                    fr["bits"] = []
                    self._cmd_decoded(fr, qpi)
            return
        kind = fr["kind"]
        if kind == "none":
            self.flag(t, "SCK pulse after complete command %02x" % fr["cmd"])
        elif kind in ("rdid",):
            self._addr_bits(fr, j, sample, nib, qpi, t, serial_addr=True)
        elif kind in ("qread", "qwrite"):
            self._addr_bits(fr, j, sample, nib, qpi, t, serial_addr=False)

    def _cmd_decoded(self, fr, qpi):
        c = fr["cmd"]
        fr["kind"] = "none"
        fr["addr"] = []
        if qpi and c == 0xF5 or not qpi and c == 0x35:
            fr["kind"] = "none"
        elif c in (0x66, 0x99):
            fr["kind"] = "none"
        elif not qpi and c == 0x9F:
            fr["kind"] = "rdid"
        elif qpi and c == 0xEB:
            fr["kind"] = "qread"
        elif qpi and c in (0x38, 0x02):
            fr["kind"] = "qwrite"
        else:
            self.flag(
                fr["t_fall"],
                "unsupported %s command %02x"
                % ("quad" if qpi else "serial", c),
            )

    def _addr_bits(self, fr, j, sample, nib, qpi, t, serial_addr):
        kind = fr["kind"]
        if kind == "rdid":
            need = 24
            fr["addr"].append(sample[0])
            if len(fr["addr"]) == need:
                fr["p0"] = j + 1
                self._begin_read(fr, "rdid")
            return
        need = 6
        if len(fr["addr"]) < need:
            fr["addr"].append(nib)
            if len(fr["addr"]) == need:
                a = 0
                for n in fr["addr"]:
                    a = a << 4 | n
                fr["a"] = a
                if kind == "qread":
                    fr["p0"] = j + 1 + 6
                    self._begin_read(fr, "qread")
            return
        if kind == "qwrite":
            fr.setdefault("wn", []).append(nib)
            if len(fr["wn"]) == 2:
                a = fr["a"]
                self.mem[a] = fr["wn"][0] << 4 | fr["wn"][1]
                fr["wn"] = []
                fr["wbytes"] += 1
                nxt = (a + 1) % SIZE
                if nxt % PAGE == 0:
                    self.crossed_page = True
                    if (
                        self.last_rise is not None
                        and fr["rises"][-1] - fr["rises"][-2]
                        < T_CLK_PAGE - 1e-9
                    ):
                        self.flag(t, "burst crossed a page below 84 MHz")
                fr["a"] = nxt

    def _begin_read(self, fr, kind):
        if kind == "rdid":
            data = bytes([self.mfid, self.kgd]) + self.eid
            if self.qpi:
                self.flag(fr["t_fall"], "read ID in quad mode")

            def value(i, pin):
                if self.absent:
                    return 0
                if i is None:
                    return 1 - 0
                byte = data[(i // 8) % len(data)]
                bit = (byte >> (7 - i % 8)) & 1
                return bit if pin == 1 else 0

            fr["driving"] = [1]
        else:
            a0 = fr["a"]

            def value(i, pin):
                if i is None:
                    return 1
                a = (a0 + i // 2) % SIZE
                nib = self.mem[a] >> 4 if i % 2 == 0 else self.mem[a] & 15
                if self.stuck_low_sio == pin:
                    return 0
                return (nib >> pin) & 1

            fr["driving"] = [0, 1, 2, 3]
        fr["value"] = value

    def _finish_frame(self, fr, t):
        cmd = fr["cmd"]
        info = {
            "cmd": cmd,
            "kind": fr.get("kind"),
            "pulses": fr["pulses"],
            "addr": fr.get("a"),
            "t_fall": fr["t_fall"],
            "t_rise": t,
            "wbytes": fr["wbytes"],
            "samples": fr["samples"],
            "rises": fr["rises"],
            "falls": fr["falls"],
            "pauses": fr["pauses"],
            "bits": list(fr["bits"]),
            "qpi": fr.get("qpi"),
        }
        self.frames.append(info)
        qpi = fr.get("qpi", self.qpi)
        if cmd is None:
            if fr["pulses"] and fr["pulses"] < (2 if qpi else 8):
                pass  # a command cut short is not executed
            return
        if fr["kind"] == "none":
            expect = 2 if qpi else 8
            if fr["pulses"] != expect:
                self.flag(
                    t,
                    "command %02x had %d clocks, expected %d"
                    % (cmd, fr["pulses"], expect),
                )
        if cmd == 0x66:
            self.rst_en = True
            return
        if cmd == 0x99 and self.rst_en:
            self.qpi = False
            self.rst_en = False
            return
        self.rst_en = False
        if not qpi and cmd == 0x35:
            self.qpi = True
        elif qpi and cmd == 0xF5:
            self.qpi = False


class Pio:
    """One state machine of a PIO block with the registers the driver
    writes, driving a Device."""

    def __init__(
        self,
        dev,
        out_ns=3.0,
        in_ns=3.0,
        access_ticks=6,
        out_gated=False,
        in_gated=False,
    ):
        self.dev = dev
        self.out_ns, self.in_ns = out_ns, in_ns
        self.out_gated, self.in_gated = out_gated, in_gated
        self.access_ticks = access_ticks
        self.imem = [0] * 32
        self.div = 1
        self.execctrl = 0x1F << 12
        self.shiftctrl = 0
        self.pinctrl = 0
        self.enabled = False
        self.now = 0.0
        self.k = 0  # next state machine cycle
        self.pc = 0
        self.x = self.y = 0
        self.osr = self.isr = 0
        self.osr_cnt = 32
        self.isr_cnt = 0
        self.delay = 0
        self.tx, self.rx = [], []
        self.rxstall = False
        self.txover = False
        self.level = 0  # pad levels from OUT/SET/side-set
        self.dirs = 0
        self.pad_hist = []  # (cycle, cs, sck, lvl, dr)
        self.stalled_in = False
        self.in_val = None
        self.last_event = -1e18
        self.emitted = (0, 0)
        self.exec_log = []
        self.pin_setup = []
        self.irq_depth = 0
        self.irq_open = None
        self.irq_spans = []
        self.delays_us = []
        self.reg_accesses = 0

    # -- configuration -----------------------------------------------------
    @property
    def cyc(self):
        return self.div * SYS_NS

    @property
    def out_latency(self):
        """Output register stage: one clk_sys cycle, or one state machine
        cycle when the stage advances on the clock enable."""
        return self.cyc if self.out_gated else OUT_STAGES * SYS_NS

    @property
    def in_latency(self):
        """Two synchronizer flip-flops on clk_sys plus the input register
        stage, which is one clk_sys cycle or one state machine cycle."""
        stage = self.cyc if self.in_gated else SYS_NS
        return (IN_STAGES - 1) * SYS_NS + stage

    def _pull_thresh(self):
        return ((self.shiftctrl >> 25) & 31) or 32

    def _push_thresh(self):
        return ((self.shiftctrl >> 20) & 31) or 32

    @property
    def autopull(self):
        return bool(self.shiftctrl >> 17 & 1)

    @property
    def autopush(self):
        return bool(self.shiftctrl >> 16 & 1)

    @property
    def left_out(self):
        return not (self.shiftctrl >> 19 & 1)

    @property
    def left_in(self):
        return not (self.shiftctrl >> 18 & 1)

    def _tx_depth(self):
        return 8 if self.shiftctrl >> 30 & 1 else 4

    def _rx_depth(self):
        return 8 if self.shiftctrl >> 31 & 1 else 4

    # -- register interface (psram_pio.h offsets) ---------------------------
    def read(self, off):
        self.advance(self.access_ticks)
        self.reg_accesses += 1
        if off == 0x004:
            v = 0
            v |= (1 << 16) if len(self.tx) >= self._tx_depth() else 0
            v |= (1 << 24) if not self.tx else 0
            v |= (1 << 8) if not self.rx else 0
            v |= 1 if len(self.rx) >= self._rx_depth() else 0
            return v
        if off == 0x008:
            return (1 if self.rxstall else 0) | (1 << 16 if self.txover else 0)
        if off == 0x020:
            if not self.rx:
                raise RuntimeError("RX FIFO read while empty")
            return self.rx.pop(0)
        if off == 0x0D4:
            return self.pc
        raise RuntimeError("read of unmodeled PIO register %#x" % off)

    def write(self, off, v):
        self.advance(self.access_ticks)
        self.reg_accesses += 1
        if off == 0x000:
            if v & 0x10:
                self.osr_cnt, self.isr_cnt, self.isr = 32, 0, 0
                self.delay = 0
                self.stalled_in = False
            self.enabled = bool(v & 1)
        elif off == 0x008:
            if v & 1:
                self.rxstall = False
        elif off == 0x010:
            if len(self.tx) >= self._tx_depth():
                self.txover = True
            else:
                self.tx.append(v & 0xFFFFFFFF)
        elif 0x048 <= off < 0x048 + 4 * 32 and off % 4 == 0:
            self.imem[(off - 0x048) // 4] = v & 0xFFFF
        elif off == 0x0C8:
            self.div = (v >> 16) or 65536
            if v & 0xFFFF:
                raise RuntimeError("fractional clock divider not modeled")
        elif off == 0x0CC:
            self.execctrl = v
        elif off == 0x0D0:
            if (v ^ self.shiftctrl) >> 30:
                self.tx, self.rx = [], []
            self.shiftctrl = v
        elif off == 0x0D8:
            self._force(v & 0xFFFF)
        elif off == 0x0DC:
            self.pinctrl = v
        else:
            raise RuntimeError("write of unmodeled PIO register %#x" % off)

    # -- time -----------------------------------------------------------------
    def advance(self, ticks):
        self.now += ticks * SYS_NS
        while self.k * self.cyc <= self.now:
            self._cycle(self.k)
            self.k += 1

    def _emit(self, cycle):
        """The pad state after the instruction of `cycle`; a cycle that
        changes no pad drives nothing new."""
        if (self.level, self.dirs) == self.emitted:
            return
        self.emitted = (self.level, self.dirs)
        cs = self.level >> 20 & 1
        sck = self.level >> 21 & 1
        lvl = self.level >> 2 & 15
        dr = self.dirs >> 2 & 15
        t = cycle * self.cyc + self.out_latency + self.out_ns
        t = max(t, self.last_event)
        self.last_event = t
        self.pad_hist.append((cycle + 1, cs, sck, lvl, dr))
        self.dev.pad_event(t, cs, sck, lvl, dr)

    # -- execution ---------------------------------------------------------
    def _side_bits(self):
        return (self.pinctrl >> 29) & 7

    def _side(self, f):
        base = (self.pinctrl >> 10) & 31
        cnt = f["side_bits"]
        if cnt == 0:
            return
        mask = ((1 << cnt) - 1) << base
        self.level = (self.level & ~mask) | (f["side"] << base)

    def _out_pins(self, value, pindirs=False):
        base = self.pinctrl & 31
        cnt = (self.pinctrl >> 20) & 63
        mask = (((1 << cnt) - 1) << base) & 0xFFFFFFFF
        v = (value << base) & mask
        if pindirs:
            self.dirs = (self.dirs & ~mask) | v
        else:
            self.level = (self.level & ~mask) | v

    def _set_pins(self, value, pindirs=False):
        base = (self.pinctrl >> 5) & 31
        cnt = (self.pinctrl >> 26) & 7
        mask = (((1 << cnt) - 1) << base) & 0xFFFFFFFF
        v = (value << base) & mask
        if pindirs:
            self.dirs = (self.dirs & ~mask) | v
        else:
            self.level = (self.level & ~mask) | v

    def _sample_pins(self, cycle):
        """The pin values an IN executed in `cycle` sees."""
        t = cycle * self.cyc - self.in_latency - self.in_ns
        in_base = (self.pinctrl >> 15) & 31
        v = 0
        for i in range(4):
            pin = in_base + i
            if not 2 <= pin <= 5:
                raise RuntimeError("input pin outside SIO0-3")
            v |= self.dev.sio_value(pin - 2, t) << i
        return v

    def _pull(self):
        self.osr = self.tx.pop(0)
        self.osr_cnt = 0

    def _shift_out(self, count):
        if self.left_out:
            v = (self.osr >> (32 - count)) & ((1 << count) - 1)
            self.osr = (self.osr << count) & 0xFFFFFFFF
        else:
            v = self.osr & ((1 << count) - 1)
            self.osr >>= count
        self.osr_cnt = min(32, self.osr_cnt + count)
        return v

    def _shift_in(self, v, count):
        v &= (1 << count) - 1
        if self.left_in:
            self.isr = ((self.isr << count) | v) & 0xFFFFFFFF
        else:
            self.isr = (self.isr >> count) | (v << (32 - count))
        self.isr_cnt = min(32, self.isr_cnt + count)

    def _cycle(self, cycle):
        if not self.enabled:
            return
        if self.delay:
            self.delay -= 1
            self._background_pull()
            return
        word = self.imem[self.pc]
        f = decode(word, self._side_bits())
        done, jumped = self._execute(word, f, cycle)
        if not done:
            self._emit(cycle)
            return
        if not jumped:
            if self.pc == ((self.execctrl >> 12) & 31):
                self.pc = (self.execctrl >> 7) & 31
            else:
                self.pc = (self.pc + 1) & 31
        self.delay = f["delay"]
        self._emit(cycle)

    def _background_pull(self):
        if self.autopull and self.osr_cnt >= self._pull_thresh() and self.tx:
            self._pull()

    def _execute(self, word, f, cycle):
        """Run the instruction at pc. Returns (completed, jumped)."""
        op, arg = f["op"], f["arg"]
        hi, lo = arg >> 5, arg & 0x1F
        if not (op == 3 and self.autopull):
            self._background_pull()
        self._side(f)
        if op == 0:  # jmp
            c = hi
            take = (
                c == 0
                or (c == 1 and self.x == 0)
                or (c == 3 and self.y == 0)
                or (c == 2 and self.x != 0)
                or (c == 4 and self.y != 0)
            )
            if c == 2:
                self.x = (self.x - 1) & 0xFFFFFFFF
            if c == 4:
                self.y = (self.y - 1) & 0xFFFFFFFF
            if c in (5, 6, 7):
                raise RuntimeError("jmp condition %d not modeled" % c)
            if take:
                self.pc = lo
            return True, take
        if op == 2:  # in
            if hi != 0:
                raise RuntimeError("in source not modeled")
            if not self.stalled_in:
                self.in_val = self._sample_pins(cycle)
                self._shift_in(self.in_val, lo or 32)
            if self.autopush and self.isr_cnt >= self._push_thresh():
                if len(self.rx) >= self._rx_depth():
                    self.rxstall = True
                    self.stalled_in = True
                    return False, False
                self.rx.append(self.isr)
                self.isr, self.isr_cnt = 0, 0
            self.stalled_in = False
            return True, False
        if op == 3:  # out
            n = lo or 32
            if self.autopull and self.osr_cnt >= self._pull_thresh():
                if self.tx:
                    self._pull()
                return False, False
            v = self._shift_out(n)
            dest = OUT_DEST[hi]
            if dest == "pins":
                self._out_pins(v)
            elif dest == "x":
                self.x = v
            elif dest == "y":
                self.y = v
            elif dest == "null":
                pass
            elif dest == "isr":
                self.isr, self.isr_cnt = v, n
            else:
                raise RuntimeError("out destination %s not modeled" % dest)
            if (
                self.autopull
                and self.osr_cnt >= self._pull_thresh()
                and self.tx
            ):
                self._pull()
            return True, False
        if op == 4:  # pull block
            if self.autopull and self.osr_cnt == 0:
                return True, False
            if not self.tx:
                return False, False
            self._pull()
            return True, False
        if op == 5:  # mov
            src = MOV_SRC[arg & 7]
            val = {"x": self.x, "y": self.y, "isr": self.isr, "null": 0}.get(
                src
            )
            if val is None:
                raise RuntimeError("mov source %s not modeled" % src)
            dest = MOV_DEST[hi]
            if dest == "x":
                self.x = val
            elif dest == "y":
                self.y = val
            elif dest == "isr":
                self.isr, self.isr_cnt = val, 0
            else:
                raise RuntimeError("mov destination %s not modeled" % dest)
            return True, False
        if op == 7:  # set
            dest = SET_DEST[hi]
            if dest == "pins":
                self._set_pins(lo)
            elif dest == "pindirs":
                self._set_pins(lo, pindirs=True)
            elif dest == "x":
                self.x = lo
            elif dest == "y":
                self.y = lo
            else:
                raise RuntimeError("set destination not modeled")
            return True, False
        raise RuntimeError("opcode %d not modeled" % op)

    def _force(self, word):
        """A word written to SMx_INSTR executes at once."""
        f = decode(word, self._side_bits())
        cycle = max(self.k - 1, 0)
        done, jumped = self._execute(word, f, cycle)
        if not done:
            raise RuntimeError("forced instruction stalled")
        self._emit(cycle)

    # -- CPU-side hooks used by the host harness ------------------------------
    def irq_save(self):
        self.irq_depth += 1
        if self.irq_depth == 1:
            self.irq_open = self.now
        return self.irq_depth

    def irq_restore(self, s):
        self.irq_depth -= 1
        if self.irq_depth == 0:
            self.irq_spans.append(self.now - self.irq_open)

    def delay_us(self, us):
        self.delays_us.append(us)
        self.now += us * 1000.0
        # run the state machine through the delay without per-cycle cost
        while self.k * self.cyc <= self.now:
            self._cycle(self.k)
            self.k += 1
