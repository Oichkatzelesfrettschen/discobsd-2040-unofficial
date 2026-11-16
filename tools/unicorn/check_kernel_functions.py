#!/usr/bin/env python3
"""Property tests of a linked rp2040 kernel's own Thumb code under Unicorn.

    check_kernel_functions.py --kernel unix.elf [--seed N] [--examples N]
        [--mutant-examples N] [--no-mutants] [--only NAME]

Each property calls one function of the linked kernel on an emulated
Cortex-M0 (kernel_emulator.py) with inputs Hypothesis draws, and compares
the result with a Python reference:

  hsx_dec_run differential   machine/hsx_decoder.h stream layout, through
                             heatshrink_model.decode, for any lookahead
                             1-8, chunked input, linear and ring output
  hsx_dec_run memory         the same calls on arbitrary bytes, checking
                             only that every access stays in the chunk,
                             the struct fields, the bytes already produced
                             and the output span
  heatshrink round trip      the linked heatshrink encoder's stream decodes
                             to its input through the model and the kernel
  hsx_expand contract        machine/hsx_decoder.h: 0 exactly when the
                             stream yields rlen bytes, CRC-32 of them, the
                             fill() callback asked only for [off, off+clen)
  crc32_update               zlib.crc32 (sys/sys/exec_hsaout.h)
  board_crc32_run            zlib.crc32, the copy in board.c
  bzero, bcopy, bcmp         bstring(3) (share/man/man3/bstring.3) and the
                             machdep.c comments: bcopy for disjoint spans

Calibration: before a verdict counts, every property runs against each
mutant kernel of its function, the linked bytes with one conditional
branch inverted or one instruction replaced by NOP (push, pop and the
return excluded), and each corrupted crc32_nibble table copy. A property
calibrates only when it kills a mutant in the failure class of its
obligation: a wrong result for the differential, round trip, CRC and
bstring properties, an access violation, fault or misaligned access for the
memory property, and both a wrong result and an access or contract breach
for hsx_expand. A kill by an instruction budget, a callee-saved register or
another class does not count toward those. The seed is fixed (--seed), the
example database is off, and a failure prints the seed.
Exit status: 0 pass, 1 a property or calibration failed, 2 setup.
"""

from __future__ import annotations

import argparse
import hashlib
import platform
import re
import struct
import sys
import tempfile
import time
import zlib
from pathlib import Path

# The sibling modules load without leaving __pycache__ in the checkout.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    import capstone
    import elftools
    import heatshrink_model
    import hypothesis
    import unicorn
    from hypothesis import HealthCheck, Phase, example, given, settings
    from hypothesis import strategies as st
    from hypothesis.configuration import set_hypothesis_home_dir
    from kernel_emulator import (
        NOP,
        CallFailure,
        KernelEmulator,
        KernelImage,
        Policy,
        armv6m_check,
        disassemble,
        literal_words,
        subset_probe,
        thumb2_probe,
    )
except ImportError as missing:
    print(
        f"unicorn-kernel: missing module ({missing}); needs hypothesis, "
        "unicorn, capstone and pyelftools under PYTHON",
        file=sys.stderr,
    )
    raise SystemExit(2) from missing

TREE = Path(__file__).resolve().parents[2]
U32 = 0xFFFFFFFF

# struct hsx_dec (machine/hsx_decoder.h): eight words, then win[512].
DEC_FIELDS = 32
DEC_WIN = 32
DEC_SIZE = DEC_FIELDS + 512
WORK_SIZE = 1024
STAGE = WORK_SIZE - DEC_SIZE
RING_MASK = 511
# decode_case draws the room of its first MAX_DRAWN_CALLS calls; every later
# call asks for all the space left, so a stream always runs to its stall or
# to a full destination. MAX_CALLS bounds the whole decode and fails past it.
MAX_DRAWN_CALLS = 48
MAX_CALLS = 1000
LINEAR_CAP = 3000


def header_define(path: str, name: str) -> int:
    text = (TREE / path).read_text()
    found = re.findall(rf"#define\s+{name}\s+(\d+)", text)
    if len(found) != 1:
        raise CallFailure("setup", f"{name} not defined once in {path}")
    return int(found[0])


HSX_LOOKAHEAD = header_define("sys/sys/exec_hsaout.h", "HSX_LOOKAHEAD")
ENCODER_LOOKAHEAD = header_define(
    "sys/arch/rp2040/heatshrink/heatshrink_config.h", "HEATSHRINK_STATIC_LOOKAHEAD_BITS"
)


class Mismatch(AssertionError):
    """The kernel's result differs from the reference."""


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise Mismatch(message)


# Strategies.


def tokens_strategy(lookahead: int, least: int, most: int):
    literal = st.tuples(st.just("lit"), st.integers(0, 255))
    reference = st.tuples(
        st.just("ref"),
        st.one_of(st.integers(1, 16), st.integers(1, 512)),
        st.integers(1, 1 << lookahead),
    )
    return st.lists(st.one_of(literal, reference), min_size=least, max_size=most)


@st.composite
def stream_strategy(draw, lookahead=None, most=500, least=0):
    """(lookahead, stream bytes, chunk cut points, expansion); a stream of
    least to most bytes, raw, or serialized from least / 2 to most / 2
    tokens, when
    expansion holds the bytes the tokens stand for (None for raw bytes)."""
    if lookahead is None:
        lookahead = draw(st.one_of(st.sampled_from([3, 8]), st.integers(1, 8)))
    expansion = None
    if draw(st.booleans()):
        stream = draw(st.binary(min_size=least, max_size=most))
    else:
        tokens = draw(tokens_strategy(lookahead, least // 2, most // 2))
        stream = heatshrink_model.encode_tokens(tokens, lookahead, draw(st.integers(0, 255)))
        expansion = heatshrink_model.expand_tokens(tokens)
    cuts = sorted(set(draw(st.lists(st.integers(0, len(stream)), max_size=5))))
    return lookahead, stream, cuts, expansion


def chunks_of(stream: bytes, cuts: list[int]) -> list[bytes]:
    edges = [0] + cuts + [len(stream)]
    return [stream[a:b] for a, b in zip(edges, edges[1:]) if b > a] or [b""]


# The decoder session: one stream decoded through hsx_dec_run with the
# byte-exact policy the header's contract allows.


class DecoderSession:
    def __init__(
        self, emu: KernelEmulator, lookahead: int, ring: bool, cap: int, low: bool
    ) -> None:
        self.emu = emu
        self.ring = ring
        self.lookahead = lookahead
        self.d = emu.alloc(DEC_SIZE)
        self.win = self.d + DEC_WIN
        self.cap = cap
        self.out = 0 if ring else emu.alloc(cap, low=low)
        self.offered = b""
        self.produced = 0
        self.chunk = (0, 0)
        self.written = bytearray(512)
        self.high = self.out
        emu.call(
            "hsx_dec_init",
            self.d,
            lookahead,
            policy=Policy(writes=[(self.d, self.d + DEC_FIELDS)]),
            budget=200,
        )
        fields = struct.unpack("<8I", emu.read(self.d, DEC_FIELDS))
        expect(
            fields == (0, 0, 0, 24, 0, 0, 0, lookahead),
            f"hsx_dec_init left fields {fields}, expected ip=ie=acc=0 sh=24 "
            f"pos=cnt=off=0 lb={lookahead}",
        )

    def offer(self, data: bytes) -> None:
        # hsx_dec_input is an inline function: the caller stores ip and ie.
        address = self.emu.alloc(len(data))
        self.emu.write(address, data)
        self.emu.write(self.d, struct.pack("<II", address, address + len(data)))
        self.chunk = (address, address + len(data))
        self.offered += data

    def _record(self, address: int, size: int) -> None:
        if self.ring:
            for slot in range(address - self.win, address - self.win + size):
                self.written[slot] = 1
        else:
            self.high = max(self.high, address + size)

    def _check_read(self, address: int, size: int) -> str | None:
        if self.ring and self.win <= address < self.win + 512:
            if not all(self.written[address - self.win : address - self.win + size]):
                return f"read of ring slot {address - self.win} before any byte was written there"
        elif not self.ring and self.out <= address < self.out + self.cap:
            if address + size > self.high:
                return (
                    f"read at output+{address - self.out} beyond the "
                    f"{self.high - self.out} bytes produced"
                )
        return None

    def space(self) -> int:
        if self.ring:
            return 512 - (self.produced & RING_MASK)
        return self.cap - self.produced

    def run(self, room: int, check_output: bool) -> int:
        start = self.produced
        if self.ring:
            base, mask = self.win, RING_MASK
            span = (self.win + (start & RING_MASK), self.win + (start & RING_MASK) + room)
            reads = [(self.win, self.win + 512)]
        else:
            base, mask = self.out, U32
            span = (self.out + start, self.out + start + room)
            reads = [(self.out, self.out + self.cap)]
        policy = Policy(
            reads=[self.chunk, (self.d, self.d + DEC_FIELDS)] + reads,
            writes=[(self.d, self.d + DEC_FIELDS), span],
            check_read=self._check_read,
            on_write=self._record,
        )
        budget = 4000 + 60 * room + 200 * (self.chunk[1] - self.chunk[0])
        n = self.emu.call("hsx_dec_run", self.d, base, mask, room, policy=policy, budget=budget)
        expect(n <= room, f"hsx_dec_run returned {n} for room {room}")
        self.produced += n
        pos = self.emu.word(self.d + 16)
        expect(pos == self.produced, f"d->pos {pos} after {self.produced} bytes")
        if check_output:
            model = heatshrink_model.decode(self.offered, self.lookahead)
            want = model[start : start + room]
            got = self.emu.read(span[0], n)
            expect(
                n == len(want) and got == want,
                f"hsx_dec_run at byte {start}, room {room}: {n} bytes {got[:24].hex()} "
                f"but the model yields {len(want)} bytes {want[:24].hex()}",
            )
        return n


def decode_case(emu: KernelEmulator, case, data, check_output: bool) -> None:
    lookahead, stream, cuts, expansion = case
    if expansion is not None:
        # The model reads the serialized tokens back to what they stand for.
        expect(
            heatshrink_model.decode(stream, lookahead) == expansion,
            "the model's decode of serialized tokens differs from their expansion",
        )
    ring = data.draw(st.booleans(), label="ring")
    cap = 0 if ring else data.draw(st.integers(0, LINEAR_CAP), label="cap")
    low = data.draw(st.booleans(), label="output flush low")
    session = DecoderSession(emu, lookahead, ring, cap, low)
    calls = 0
    for chunk in chunks_of(stream, cuts):
        session.offer(chunk)
        while True:
            space = session.space()
            if not ring and space == 0:
                return
            if calls < MAX_DRAWN_CALLS:
                least = 1 if ring else 0
                room = data.draw(
                    st.one_of(
                        st.just(space),
                        st.integers(least, min(space, 16)),
                        st.integers(least, space),
                    ),
                    label="room",
                )
            else:
                room = space
            calls += 1
            expect(calls <= MAX_CALLS, f"the decode of one stream passed {MAX_CALLS} calls")
            if session.run(room, check_output) < room:
                # A stall leaves no whole token in the chunk, so a second
                # call on the same chunk produces nothing.
                if data.draw(st.booleans(), label="call again") and session.space():
                    again = session.run(session.space(), check_output)
                    expect(again == 0, f"{again} bytes from a chunk that had stalled")
                break
        if check_output:
            model = heatshrink_model.decode(session.offered, lookahead)
            expect(
                session.produced == len(model),
                f"stalled after {session.produced} bytes, model holds {len(model)}",
            )


# Lengths and alignments for the bstring(3) routines: both sides of the
# 16-byte threshold of their word loops, and word-aligned pointers half the
# time, the condition for those loops.
LENGTHS = st.one_of(st.integers(0, 15), st.integers(16, 300))
SKEWS = st.sampled_from([0, 0, 0, 1, 2, 3])


# Properties. Each takes the emulator and the example count and raises on
# the first failure.


def prop_decoder(check_output: bool):
    def build(emu: KernelEmulator):
        @given(case=stream_strategy(), data=st.data())
        def test(case, data):
            emu.release()
            decode_case(emu, case, data, check_output)

        return test

    return build


def run_encoder(emu: KernelEmulator, source: bytes) -> bytes:
    enc = emu.alloc(2048)
    src = emu.alloc(max(1, len(source)))
    emu.write(src, source)
    out = emu.alloc(4096)
    count = emu.alloc(4)
    emu.call("heatshrink_encoder_reset", enc, policy=None, budget=10_000)
    stream = bytearray()

    def poll() -> None:
        while True:
            emu.write(count, bytes(4))
            result = emu.call(
                "heatshrink_encoder_poll", enc, out, 4096, count, policy=None, budget=10_000_000
            )
            stream.extend(emu.read(out, emu.word(count)))
            if result == 0:
                return
            if result != 1:
                raise CallFailure("setup", f"encoder poll returned {result:#x}")

    offset = 0
    while offset < len(source):
        emu.write(count, bytes(4))
        if (
            emu.call(
                "heatshrink_encoder_sink",
                enc,
                src + offset,
                len(source) - offset,
                count,
                policy=None,
                budget=100_000,
            )
            != 0
        ):
            raise CallFailure("setup", "encoder sink refused input")
        offset += emu.word(count)
        poll()
    while (result := emu.call("heatshrink_encoder_finish", enc, policy=None, budget=10_000)) == 1:
        poll()
    if result != 0:
        raise CallFailure("setup", f"encoder finish returned {result:#x}")
    return bytes(stream)


def prop_round_trip(emu: KernelEmulator):
    @given(
        source=st.one_of(
            st.binary(max_size=400),
            st.lists(st.sampled_from(b"ab \n\x00"), max_size=600).map(bytes),
        ),
        data=st.data(),
    )
    def test(source, data):
        emu.release()
        stream = run_encoder(emu, source)
        expect(
            heatshrink_model.decode(stream, ENCODER_LOOKAHEAD) == source,
            f"model decode of the linked encoder's {len(stream)}-byte stream "
            "differs from its input",
        )
        cuts = sorted(set(data.draw(st.lists(st.integers(0, len(stream)), max_size=3))))
        session = DecoderSession(emu, ENCODER_LOOKAHEAD, False, len(source), False)
        for chunk in chunks_of(stream, cuts):
            session.offer(chunk)
            while session.space():
                room = session.space()
                if session.run(room, True) < room:
                    break
        got = emu.read(session.out, len(source))
        expect(
            session.produced == len(source) and got == source,
            f"kernel decode of the encoder stream yields {session.produced} of {len(source)} bytes",
        )

    return test


def prop_expand(emu: KernelEmulator):
    # Streams past one stage (HSX_STAGE bytes) take several fill() calls,
    # and outputs past one window wrap the ring.
    @given(
        case=st.one_of(
            stream_strategy(lookahead=HSX_LOOKAHEAD, most=400),
            stream_strategy(lookahead=HSX_LOOKAHEAD, least=2 * STAGE, most=1500),
        ),
        data=st.data(),
    )
    def test(case, data):
        emu.release()
        _, stream, _, _ = case
        full = heatshrink_model.decode(stream, HSX_LOOKAHEAD)
        rlen = data.draw(
            st.one_of(
                st.just(len(full)),
                st.just(len(full)),
                st.integers(max(0, len(full) - 3), len(full) + 3),
                st.integers(0, 2000),
            ),
            label="rlen",
        )
        use_dst = data.draw(st.booleans(), label="dst")
        low = data.draw(st.booleans(), label="dst flush low")
        base = data.draw(st.integers(0, 1 << 20), label="file offset")
        work = emu.alloc(WORK_SIZE)
        stage = work + DEC_SIZE
        fail_at = data.draw(st.one_of(st.none(), st.integers(0, 3)), label="failing fill")
        dst = emu.alloc(rlen, low=low) if use_dst else 0
        crc_word = emu.alloc(4)
        ctx = 0x5EED0000
        high = [dst]
        fills = [0]
        # The work-buffer bytes the kernel or the fill callback has defined:
        # a read of any other byte (an unfilled stage byte, one left by an
        # earlier fill, a window slot not yet written) breaks the contract.
        defined = bytearray(WORK_SIZE)

        def fill(context: int, offset: int, buffer: int, length: int) -> int:
            fills[0] += 1
            if fail_at is not None and fills[0] == fail_at + 1:
                return U32
            if context != ctx or buffer != stage or not 0 < length <= STAGE:
                emu.fail(f"fill(ctx {context:#x}, buf {buffer:#x}, len {length}) outside the stage")
                return U32
            if offset < base or offset + length > base + len(stream):
                emu.fail(
                    f"fill asked for [{offset - base}, {offset - base + length}) "
                    f"of a {len(stream)}-byte stream"
                )
                return U32
            emu.write(buffer, stream[offset - base : offset - base + length])
            defined[DEC_SIZE:] = bytes(STAGE)
            defined[DEC_SIZE : DEC_SIZE + length] = b"\x01" * length
            return 0

        def record(address: int, size: int) -> None:
            if dst <= address < dst + rlen:
                high[0] = max(high[0], address + size)
            if work <= address < work + WORK_SIZE:
                defined[address - work : address - work + size] = b"\x01" * size

        def check_read(address: int, size: int) -> str | None:
            if dst and dst <= address < dst + rlen and address + size > high[0]:
                return f"read at dst+{address - dst} beyond the bytes produced"
            if work <= address < work + WORK_SIZE and not all(
                defined[address - work : address - work + size]
            ):
                # Once dst holds rlen bytes, hsx_expand decodes on into the
                # window to find a token past the promised length; the
                # window then holds no output, and any byte it produces
                # fails the stream whatever the history it read. Nothing
                # else may read an undefined byte.
                probing = bool(dst) and high[0] == dst + rlen
                if not (
                    probing and work + DEC_FIELDS <= address and address + size <= work + DEC_SIZE
                ):
                    return f"read of work+{address - work} before anything defined it"
            return None

        writes = [(work, work + DEC_SIZE), (crc_word, crc_word + 4)]
        if dst:
            writes.append((dst, dst + rlen))
        policy = Policy(
            reads=[(work, work + WORK_SIZE)] + ([(dst, dst + rlen)] if dst else []),
            writes=writes,
            check_read=check_read,
            on_write=record,
        )
        callback = emu.callback(fill)
        budget = 20_000 + 400 * (rlen + len(stream))
        result = emu.call(
            "hsx_expand",
            work,
            callback,
            ctx,
            base,
            len(stream),
            dst,
            rlen,
            crc_word,
            policy=policy,
            budget=budget,
        )
        ok = len(full) == rlen
        refused = fail_at is not None and fail_at < -(-len(stream) // STAGE)
        expect(
            result == (0 if ok and not refused else U32),
            f"hsx_expand returned {result:#x} for a stream of {len(full)} bytes, rlen {rlen}"
            + (f", fill call {fail_at} failing" if refused else ""),
        )
        if ok and not refused:
            crc = emu.word(crc_word)
            expect(
                crc == zlib.crc32(full), f"*crc {crc:#010x}, zlib.crc32 {zlib.crc32(full):#010x}"
            )
            if dst:
                expect(emu.read(dst, rlen) == full, "dst differs from the model's bytes")

    return test


def prop_crc(name: str):
    def build(emu: KernelEmulator):
        @given(payload=st.binary(max_size=1200), start=st.integers(0, U32), low=st.booleans())
        @example(payload=b"123456789", start=U32, low=False)
        def test(payload, start, low):
            emu.release()
            buf = emu.alloc(len(payload), low=low)
            emu.write(buf, payload)
            got = emu.call(
                name,
                start,
                buf,
                len(payload),
                policy=Policy(reads=[(buf, buf + len(payload))]),
                budget=200 + 40 * len(payload),
            )
            want = zlib.crc32(payload, start ^ U32) ^ U32
            expect(
                got == want,
                f"{name}({start:#x}, {len(payload)} bytes) = {got:#x}, zlib gives {want:#x}",
            )
            if payload == b"123456789" and start == U32:
                expect(got ^ U32 == 0xCBF43926, "CRC-32 check value of '123456789' is 0xcbf43926")

        return test

    return build


def prop_bzero(emu: KernelEmulator):
    @given(length=LENGTHS, skew=SKEWS)
    def test(length, skew):
        emu.release()
        area = emu.alloc(length + 8, low=True)
        dst = area + 4 + skew
        emu.call(
            "bzero",
            dst,
            length,
            policy=Policy(writes=[(dst, dst + length)]),
            budget=200 + 10 * length,
        )
        image = emu.read(area, length + 8)
        want = b"\xa5" * (dst - area) + bytes(length) + b"\xa5" * (area + length + 8 - dst - length)
        expect(image == want, f"bzero(+{skew}, {length}) left {image.hex()}")

    return test


def prop_bcopy(emu: KernelEmulator):
    @given(
        payload=LENGTHS.flatmap(lambda n: st.binary(min_size=n, max_size=n)),
        sskew=SKEWS,
        dskew=SKEWS,
    )
    def test(payload, sskew, dskew):
        emu.release()
        n = len(payload)
        src = emu.alloc(n + 4, low=True) + sskew
        dst = emu.alloc(n + 4, low=True) + dskew
        emu.write(src, payload)
        emu.call(
            "bcopy",
            src,
            dst,
            n,
            policy=Policy(reads=[(src, src + n)], writes=[(dst, dst + n)]),
            budget=200 + 10 * n,
        )
        expect(emu.read(dst, n) == payload, f"bcopy of {n} bytes (+{sskew} to +{dskew}) differs")

    return test


def prop_bcmp(emu: KernelEmulator):
    @given(
        payload=LENGTHS.flatmap(lambda n: st.binary(min_size=n, max_size=n)),
        flip=st.one_of(st.none(), st.tuples(st.integers(0, 299), st.integers(1, 255))),
        askew=SKEWS,
        bskew=SKEWS,
    )
    def test(payload, flip, askew, bskew):
        emu.release()
        second = bytearray(payload)
        if flip is not None and payload:
            second[flip[0] % len(payload)] ^= flip[1]
        second = bytes(second)
        n = len(payload)
        a = emu.alloc(n + 4, low=True) + askew
        b = emu.alloc(n + 4, low=True) + bskew
        emu.write(a, payload)
        emu.write(b, second)
        got = emu.call(
            "bcmp", a, b, n, policy=Policy(reads=[(a, a + n), (b, b + n)]), budget=200 + 20 * n
        )
        expect(
            (got == 0) == (payload == second), f"bcmp returned {got:#x}, equal={payload == second}"
        )

    return test


# Failure classes of a killed mutant (the CallFailure kind, or "mismatch" for
# a wrong result the reference comparison finds). RESULT is a wrong value, ACCESS
# an access the policy refuses, a fault or a misaligned access, and CONTRACT a
# breach of the callback contract.
RESULT = frozenset({"mismatch"})
ACCESS = frozenset({"memory", "fault", "alignment"})
CONTRACT = frozenset({"contract"})

# (property name, function its mutants patch, builder, kill classes). A
# property calibrates only when, for each class set listed, at least one
# mutant is killed in a class of that set: a differential property needs a
# mutant it catches by a wrong result, not only by a fault or an exhausted
# budget that any harness would report, and the memory property a mutant it
# catches by an access violation.
PROPERTIES = (
    ("hsx_dec_run differential", "hsx_dec_run", prop_decoder(True), (RESULT,)),
    ("hsx_dec_run memory", "hsx_dec_run", prop_decoder(False), (ACCESS,)),
    ("heatshrink round trip", "hsx_dec_run", prop_round_trip, (RESULT,)),
    ("hsx_expand contract", "hsx_expand", prop_expand, (RESULT, ACCESS | CONTRACT)),
    ("crc32_update", "crc32_update", prop_crc("crc32_update"), (RESULT,)),
    ("board_crc32_run", "board_crc32_run", prop_crc("board_crc32_run"), (RESULT,)),
    ("bzero", "bzero", prop_bzero, (RESULT,)),
    ("bcopy", "bcopy", prop_bcopy, (RESULT,)),
    ("bcmp", "bcmp", prop_bcmp, (RESULT,)),
)


def code_mutants(image: KernelImage, name: str) -> list[tuple[str, list[tuple[int, bytes]]]]:
    """One mutant per conditional branch (condition inverted) and per other
    instruction (NOP), excluding push, pop and the return."""
    base = image.function(name).address
    found = []
    for address, size, mnemonic, operands, raw in disassemble(image, name):
        where = f"{name}+{address - base:#x}"
        if size == 2 and 0xD0 <= raw[1] <= 0xDD:
            found.append((f"{where} {mnemonic} inverted", [(address, bytes([raw[0], raw[1] ^ 1]))]))
        elif mnemonic in ("push", "pop") or (mnemonic == "bx" and operands == "lr"):
            continue
        else:
            found.append((f"{where} {mnemonic} {operands} removed", [(address, NOP * (size // 2))]))
    return found


def table_mutants(image: KernelImage) -> dict[str, tuple[str, list[tuple[int, bytes]]]]:
    """For each CRC function, a mutant that corrupts entry 1 of the
    crc32_nibble table its literal pool names (exactly one)."""
    tables = {s.address for s in image.objects("crc32_nibble")}
    mutants = {}
    for name in ("crc32_update", "board_crc32_run"):
        named = {w for w in literal_words(image, name).values() if w in tables}
        if len(named) != 1:
            raise CallFailure("setup", f"{name} names {len(named)} crc32_nibble tables")
        table = named.pop()
        word = struct.unpack("<I", image.read(table + 4, 4))[0]
        mutants[name] = (
            f"crc32_nibble@{table:#x}[1] corrupted",
            [(table + 4, struct.pack("<I", word ^ 0x00010000))],
        )
    return mutants


def run_property(builder, emu: KernelEmulator, examples: int, seed: int, shrink: bool):
    test = builder(emu)
    phases = (
        (Phase.explicit, Phase.generate, Phase.shrink)
        if shrink
        else (Phase.explicit, Phase.generate)
    )
    test = settings(
        max_examples=examples,
        deadline=None,
        database=None,
        print_blob=True,
        report_multiple_bugs=False,
        suppress_health_check=list(HealthCheck),
        phases=phases,
    )(test)
    test = hypothesis.seed(seed)(test)
    try:
        test()
    except CallFailure as failure:
        return failure.kind, str(failure)
    except Mismatch as failure:
        return "mismatch", str(failure)
    except Exception as failure:  # noqa: BLE001 - any other escape is a kill too
        return type(failure).__name__, str(failure)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=20261115)
    parser.add_argument("--examples", type=int, default=300)
    parser.add_argument("--mutant-examples", type=int, default=150)
    parser.add_argument("--no-mutants", action="store_true")
    parser.add_argument("--only", action="append", default=[])
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    # Hypothesis keeps a constants cache under its home directory; a
    # private temporary one keeps the checkout clean.
    home = tempfile.TemporaryDirectory(prefix="unicorn-kernel-hypothesis-")
    set_hypothesis_home_dir(home.name)

    print(
        f"unicorn-kernel: Python {platform.python_version()}, unicorn {unicorn.__version__}, "
        f"hypothesis {hypothesis.__version__}, capstone {capstone.__version__}, "
        f"pyelftools {elftools.__version__}"
    )
    if not args.kernel.is_file():
        print(f"unicorn-kernel: {args.kernel} is missing; build the kernel first", file=sys.stderr)
        return 2
    digest = hashlib.sha256(args.kernel.read_bytes()).hexdigest()
    print(f"unicorn-kernel: {args.kernel} sha256 {digest}, seed {args.seed}")
    executed, refusal = thumb2_probe()
    if not refusal:
        print("FAIL unicorn-kernel subset: the block check accepts Thumb-2 RBIT")
        return 1
    print(
        f"PASS unicorn-kernel subset: the block check refuses RBIT ({refusal}); the bare "
        f"Cortex-M0 model {'executes' if executed else 'refuses'} it"
    )
    sorted_wrong = subset_probe()
    if sorted_wrong:
        print(f"FAIL unicorn-kernel subset probe: {'; '.join(sorted_wrong)}")
        return 1
    print(
        "PASS unicorn-kernel subset probe: the block check accepts MRS and MSR of PRIMASK, "
        "CONTROL, MSP and IPSR, NOP and CPSID, and refuses BASEPRI, BASEPRI_MAX, "
        "FAULTMASK, the DSP APSR mask, RBIT, CBZ and IT"
    )

    try:
        image = KernelImage(args.kernel)
        selected = [p for p in PROPERTIES if not args.only or p[0] in args.only]
        for function in sorted({p[1] for p in selected} | {"hsx_dec_init"}):
            count = armv6m_check(image, function)
            print(f"PASS unicorn-kernel armv6m {function}: {count} instructions, all ARMv6-M")
        tables = table_mutants(image)
    except CallFailure as failure:
        print(f"FAIL unicorn-kernel setup: {failure}")
        return 2 if failure.kind == "setup" else 1

    failed = 0
    good = KernelEmulator(image)
    for name, function, builder, classes in selected:
        started = time.monotonic()
        verdict = run_property(builder, good, args.examples, args.seed, shrink=True)
        if verdict is not None:
            print(f"FAIL unicorn-kernel {name}: {verdict[0]}: {verdict[1]}")
            print(f"     reproduce: --only '{name}' --seed {args.seed} --examples {args.examples}")
            failed += 1
            continue
        print(
            f"PASS unicorn-kernel {name}: {args.examples} examples, "
            f"{time.monotonic() - started:.1f} s"
        )
        if args.no_mutants:
            continue
        mutants = code_mutants(image, function)
        extra = []
        if function in tables:
            extra = [tables[function]]
            other = tables["board_crc32_run" if function == "crc32_update" else "crc32_update"]
        counts: dict[str, int] = {}
        survivors = []
        started = time.monotonic()
        for label, patches in mutants + extra:
            outcome = run_property(
                builder,
                KernelEmulator(image, patches),
                args.mutant_examples,
                args.seed,
                shrink=False,
            )
            if outcome is None:
                survivors.append(label)
                continue
            counts[outcome[0]] = counts.get(outcome[0], 0) + 1
        killed = sum(counts.values())
        problems = []
        if killed == 0:
            problems.append("no mutant killed")
        for wanted in classes:
            if not any(counts.get(kind, 0) for kind in wanted):
                problems.append(f"no mutant killed as {' or '.join(sorted(wanted))}")
        if extra and extra[0][0] in survivors:
            problems.append(f"its own table mutant survived ({extra[0][0]})")
        if extra:
            if (
                run_property(
                    builder,
                    KernelEmulator(image, other[1]),
                    args.mutant_examples,
                    args.seed,
                    shrink=False,
                )
                is not None
            ):
                problems.append(f"the other copy's table mutant changed it ({other[0]})")
        status = "FAIL" if problems else "PASS"
        print(
            f"{status} unicorn-kernel {name} calibration: {killed}/{len(mutants) + len(extra)} "
            f"mutants killed ({', '.join(f'{k} {v}' for k, v in sorted(counts.items()))}), "
            f"{time.monotonic() - started:.1f} s" + (f"; {'; '.join(problems)}" if problems else "")
        )
        for label in survivors:
            print(f"     survivor: {label}")
        failed += bool(problems)
    print(
        f"unicorn-kernel: {len(selected) - failed} of {len(selected)} properties pass"
        + ("" if args.no_mutants else " with calibration")
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
