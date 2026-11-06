#!/usr/bin/env python3
"""Compare sys/kern/subr_crc32.c with zlib's crc32().

The kernel source is compiled as a host shared object and driven through
ctypes. zlib.crc32 is the oracle: crc32_update() carries the raw register,
so a state S corresponds to zlib's running value S ^ 0xffffffff.

    crc32_test.py --cc CC --source subr_crc32.c --include sys/
    crc32_test.py --cc CC --source subr_crc32.c --include sys/ --mutants

The second form rewrites the source once per mutant, requires that the
rewrite changed the text, and requires the comparison to fail on the
rewritten object.
"""
import argparse
import ctypes
import os
import random
import subprocess
import sys
import tempfile
import zlib

MASK = 0xFFFFFFFF

# Each mutant replaces one fragment of subr_crc32.c that occurs exactly once.
MUTANTS = (
    ("table entry 5 with a flipped bit", "0x6b6b51f4U", "0x6b6b51f5U"),
    ("table entries 1 and 2 swapped", "0x1db71064U, 0x3b6e20c8U",
     "0x3b6e20c8U, 0x1db71064U"),
    ("last table entry zero", "0xbdbdf21cU", "0x00000000U"),
    ("first shift of 3 bits", "crc = (crc >> 4) ^ crc32_nibble[crc & 15];\n"
     "        crc = (crc >> 4)",
     "crc = (crc >> 3) ^ crc32_nibble[crc & 15];\n        crc = (crc >> 4)"),
    ("second shift of 5 bits", "crc = (crc >> 4) ^ crc32_nibble[crc & 15];\n"
     "    }", "crc = (crc >> 5) ^ crc32_nibble[crc & 15];\n    }"),
    ("index mask of 7", "crc32_nibble[crc & 15];\n        crc = (crc >> 4)",
     "crc32_nibble[crc & 7];\n        crc = (crc >> 4)"),
    ("second lookup removed",
     "        crc = (crc >> 4) ^ crc32_nibble[crc & 15];\n    }", "    }"),
    ("final byte skipped", "while (len-- != 0)", "while (len-- > 1)"),
    ("input byte not mixed in", "crc ^= *p++;", "crc ^= *p++ & 0x7f;"),
)


def build(cc, source, include, out):
    cmd = [cc, "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-O2", "-fPIC",
           "-shared", "-idirafter", include, "-o", out, source]
    subprocess.run(cmd, check=True)


def load(path):
    lib = ctypes.CDLL(path)
    lib.crc32_update.restype = ctypes.c_uint
    lib.crc32_update.argtypes = [ctypes.c_uint, ctypes.c_void_p,
                                 ctypes.c_uint]
    return lib


def check(lib):
    """Return the list of failed comparisons."""
    failures = []
    rng = random.Random(0x5EED)

    def fail(what):
        if len(failures) < 20:
            failures.append(what)

    def raw(state, buf, off, length):
        base = ctypes.addressof(buf) + off
        return lib.crc32_update(state, ctypes.c_void_p(base), length)

    def expect(state, data):
        return zlib.crc32(data, state ^ MASK) ^ MASK

    # Check vector of the zlib and IEEE 802.3 documentation.
    vec = ctypes.create_string_buffer(b"123456789", 9)
    got = raw(MASK, vec, 0, 9) ^ MASK
    if got != 0xCBF43926:
        fail("check vector 123456789: got 0x%08x" % got)
    # A zero-length update leaves every state unchanged.
    one = ctypes.create_string_buffer(b"x", 1)
    for state in (0, 1, MASK, 0x12345678):
        if raw(state, one, 0, 0) != state:
            fail("empty input changed state 0x%08x" % state)
    if raw(MASK, one, 0, 0) ^ MASK != zlib.crc32(b""):
        fail("empty input finish differs from zlib")

    # Every length 0..300 of random data, from the standard start and from
    # a random state, at every start alignment 0..7.
    pad = 8
    for length in range(301):
        data = rng.randbytes(length)
        for off in range(pad):
            buf = ctypes.create_string_buffer(pad + length + 1)
            ctypes.memmove(ctypes.addressof(buf) + off, data, length)
            if raw(MASK, buf, off, length) ^ MASK != zlib.crc32(data):
                fail("length %d offset %d from the standard start"
                     % (length, off))
        state = rng.getrandbits(32)
        buf = ctypes.create_string_buffer(data, length + 1)
        if raw(state, buf, 0, length) != expect(state, data):
            fail("length %d from state 0x%08x" % (length, state))

    # Incremental updates split at every offset of a 1 KB buffer.
    data = rng.randbytes(1024)
    buf = ctypes.create_string_buffer(data, len(data))
    whole = zlib.crc32(data)
    for split in range(len(data) + 1):
        state = raw(MASK, buf, 0, split)
        state = raw(state, buf, split, len(data) - split)
        if state ^ MASK != whole:
            fail("1 KB buffer split at %d" % split)

    # All 256 single-byte inputs and the all-zero and all-ones runs exercise
    # every table index in both nibble positions.
    for value in range(256):
        b = ctypes.create_string_buffer(bytes([value]), 1)
        if raw(MASK, b, 0, 1) ^ MASK != zlib.crc32(bytes([value])):
            fail("single byte 0x%02x" % value)
    for fill in (0x00, 0xFF):
        data = bytes([fill]) * 4099
        b = ctypes.create_string_buffer(data, len(data))
        if raw(MASK, b, 0, len(data)) ^ MASK != zlib.crc32(data):
            fail("run of 0x%02x" % fill)
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--include", required=True)
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        if not args.mutants:
            so = os.path.join(tmp, "crc32.so")
            build(args.cc, args.source, args.include, so)
            failures = check(load(so))
            for f in failures:
                print("FAIL: " + f)
            if failures:
                return 1
            print("crc32: subr_crc32.c agrees with zlib")
            return 0

        with open(args.source, encoding="utf-8") as fh:
            text = fh.read()
        escaped = 0
        for n, (name, old, new) in enumerate(MUTANTS):
            if text.count(old) != 1:
                print("FAIL: mutant %r fragment occurs %d times"
                      % (name, text.count(old)))
                return 1
            src = os.path.join(tmp, "mutant%d.c" % n)
            with open(src, "w", encoding="utf-8") as fh:
                fh.write(text.replace(old, new))
            so = os.path.join(tmp, "mutant%d.so" % n)
            build(args.cc, src, args.include, so)
            failures = check(load(so))
            verdict = "rejected" if failures else "ESCAPED"
            print("mutant %-36s %s" % (name, verdict))
            if not failures:
                escaped += 1
        if escaped:
            print("FAIL: %d mutants escaped" % escaped)
            return 1
        print("crc32: all %d mutants rejected" % len(MUTANTS))
        return 0


if __name__ == "__main__":
    sys.exit(main())
