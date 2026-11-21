#!/usr/bin/env python3
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

"""Evaluate candidate encodings of the packed version integer (kern.osrev).

Each encoding maps a release (major, minor, patch, stage rank) to one 32-bit
value. The harness checks, for every encoding, the properties a consumer of
"is this kernel at least release X" relies on:

  order       packed values rise exactly as the tuples do (SemVer 2.0.0
              precedence once a stage rank is fixed), over a grid of the
              boundary values of every field
  roundtrip   unpack(pack(t)) == t on that grid
  signed      the largest value fits a signed 32-bit long (kern.osrev is read
              through sysctl_rdlong; the target long is 32 bits)
  guard       a field past its width is refused instead of carrying into its
              neighbor

It also measures the decoders of each encoding on the target compiler, when
arm-none-eabi-gcc is installed. The mutants at the end are deliberately wrong
encodings; --selftest requires the checks to reject every one of them and to
accept the reference encodings before any verdict of this file counts.

usage: encodings.py             print the results as TSV
       encodings.py --selftest  calibrate the checks, exit 1 on a failure
"""

import functools
import itertools
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

INT_MAX = 2**31 - 1


class Encoding:
    """A packing of (major, minor, patch, stage rank) into one integer.

    fields is a list of (name, weight, limit): the value of a field times its
    weight is summed, and limit is the largest value the field may take.
    final is the stage rank of a release; lower ranks are pre-releases.
    """

    def __init__(self, name, fields, final, note=""):
        self.name = name
        self.fields = fields
        self.final = final
        self.note = note

    def limits(self):
        out = {name: limit for name, _, limit in self.fields}
        out.setdefault("serial", 0)
        return out

    def pack(self, major, minor, patch, stage, serial=0):
        values = {
            "major": major,
            "minor": minor,
            "patch": patch,
            "stage": stage,
            "serial": serial,
        }
        if serial < 0 or serial > self.limits()["serial"]:
            raise OverflowError(f"{self.name}: serial {serial} exceeds its field")
        if stage == self.final and serial != 0:
            raise OverflowError(f"{self.name}: a release has no serial")
        total = 0
        for name, weight, limit in self.fields:
            value = values[name]
            if value < 0 or value > limit:
                raise OverflowError(f"{self.name}: {name} {value} exceeds {limit}")
            total += value * weight
        return total

    def unpack(self, value):
        out = {"serial": 0}
        for name, weight, limit in self.fields:
            out[name] = (value // weight) % (limit + 1)
        return (out["major"], out["minor"], out["patch"], out["stage"], out["serial"])


def binary(name, widths, final, note=""):
    """Fields from the top: widths is [(field, bits)] with the signed bit free."""
    total = sum(bits for _, bits in widths)
    shift = total
    fields = []
    for field, bits in widths:
        shift -= bits
        fields.append((field, 1 << shift, (1 << bits) - 1))
    return Encoding(name, fields, final, note)


def decimal(name, parts, final, note=""):
    """Fields from the top: parts is [(field, digits)]."""
    total = sum(digits for _, digits in parts)
    place = total
    fields = []
    for field, digits in parts:
        place -= digits
        fields.append((field, 10**place, 10**digits - 1))
    return Encoding(name, fields, final, note)


def existing_decimal():
    """The candidate of docs/research/version-scheme.md: 2.5 digits then stage."""
    return Encoding(
        "dec-existing",
        [
            ("major", 10**8, 20),
            ("minor", 10**6, 99),
            ("patch", 10, 99999),
            ("stage", 1, 9),
        ],
        9,
        "major*1e8 + minor*1e6 + patch*10 + stage (version-scheme.md)",
    )


def python_layout():
    """bin-python: the serial nibble is a field the grid leaves at 0."""
    return Encoding(
        "bin-python",
        [
            ("major", 1 << 24, 127),
            ("minor", 1 << 16, 255),
            ("patch", 1 << 8, 255),
            ("stage", 1 << 4, 15),
            ("serial", 1, 15),
        ],
        15,
        "PY_VERSION_HEX layout 0xMMmmppSs, stage then serial nibble",
    )


def reference_set():
    return [
        existing_decimal(),
        binary(
            "bin-7.8.12.4",
            [("major", 7), ("minor", 8), ("patch", 12), ("stage", 4)],
            15,
            "major 7 bits, minor 8, patch 12, stage 4",
        ),
        binary(
            "bin-7.8.11.5",
            [("major", 7), ("minor", 8), ("patch", 11), ("stage", 5)],
            31,
            "major 7 bits, minor 8, patch 11, stage 5 (initial letter a=1..z=26, release 31)",
        ),
        python_layout(),
        Encoding(
            "bin-gapped-0xMNN00PPS",
            [
                ("major", 1 << 28, 7),
                ("minor", 1 << 20, 255),
                ("patch", 1 << 4, 255),
                ("stage", 1, 15),
            ],
            15,
            "adapts OpenSSL 0xMNN00PP0L: bits 12 to 19 reserved, "
            "the low nibble carries the stage as the 1.1.1 status nibble did",
        ),
    ]


class LossyUnpack(Encoding):
    """An encoding whose unpack drops the stage: pack is right, the round trip is not."""

    def unpack(self, value):
        major, minor, patch, _stage, serial = super().unpack(value)
        return (major, minor, patch, 0, serial)


# Deliberately wrong encodings: each must fail the check named beside it.
def mutants():
    stage_high = binary(
        "mutant-stage-high",
        [("stage", 4), ("major", 3), ("minor", 8), ("patch", 16)],
        15,
        "stage above major: a pre-release of 1.0.0 sorts above 0.9.9",
    )
    overlap = Encoding(
        "mutant-overlap",
        [
            ("major", 10**8, 20),
            ("minor", 10**6, 100),
            ("patch", 10, 99999),
            ("stage", 1, 9),
        ],
        9,
        "minor limit 100: 0.100.0 packs equal to 1.0.0",
    )
    wide = Encoding(
        "mutant-signed",
        [
            ("major", 1 << 28, 15),
            ("minor", 1 << 20, 255),
            ("patch", 1 << 4, 65535),
            ("stage", 1, 15),
        ],
        15,
        "major 4 bits: 8.0.0 sets the sign bit",
    )
    final_low = Encoding(
        "mutant-final-low",
        [("major", 10**8, 20), ("minor", 10**6, 99), ("patch", 10, 99999), ("stage", 1, 9)],
        0,
        "release ranked below its pre-releases",
    )
    serial_high = Encoding(
        "mutant-serial-high",
        [
            ("major", 1 << 24, 127),
            ("minor", 1 << 16, 255),
            ("patch", 1 << 8, 255),
            ("serial", 1 << 4, 15),
            ("stage", 1, 15),
        ],
        15,
        "serial above stage: alpha.15 sorts above beta.0",
    )
    lossy = LossyUnpack(
        "mutant-lossy-unpack",
        [
            ("major", 1 << 24, 127),
            ("minor", 1 << 16, 255),
            ("patch", 1 << 4, 4095),
            ("stage", 1, 15),
        ],
        15,
        "unpack discards the stage",
    )
    return [
        (lossy, "roundtrip"),
        (serial_high, "order"),
        (stage_high, "order"),
        (overlap, "guard"),
        (wide, "signed"),
        (final_low, "order"),
    ]


def grid(encoding):
    """Boundary values of every field: 0, 1, 2, limit-1, limit; stage ranks 1..final.

    The serial counts builds inside one pre-release stage; a release has none.
    """
    lim = encoding.limits()

    def edge(limit):
        return sorted({v for v in (0, 1, 2, limit - 1, limit) if 0 <= v <= limit})

    stages = sorted({1, 2, encoding.final - 1, encoding.final})
    stages = [s for s in stages if 0 <= s <= lim["stage"]]
    points = itertools.product(
        edge(lim["major"]),
        edge(lim["minor"]),
        edge(lim["patch"]),
        stages,
        edge(lim["serial"]),
    )
    return [t for t in points if t[3] != encoding.final or t[4] == 0]


def rank_key(encoding, tup):
    """SemVer order of a tuple: the core, every pre-release below the release, serial."""
    major, minor, patch, stage, serial = tup
    return (major, minor, patch, 10**9 if stage == encoding.final else stage, serial)


def check_order(encoding):
    points = grid(encoding)
    ordered = sorted(points, key=lambda t: rank_key(encoding, t))
    values = []
    for tup in ordered:
        try:
            values.append(encoding.pack(*tup))
        except OverflowError:
            return False
    return all(a < b for a, b in zip(values, values[1:]))


def check_roundtrip(encoding):
    for tup in grid(encoding):
        try:
            if encoding.unpack(encoding.pack(*tup)) != tup:
                return False
        except OverflowError:
            return False
    return True


def largest(encoding):
    """The largest valid value: the highest core as a release, which has no serial."""
    lim = encoding.limits()
    return encoding.pack(lim["major"], lim["minor"], lim["patch"], encoding.final, 0)


def check_signed(encoding):
    return 0 <= largest(encoding) <= INT_MAX


def check_guard(encoding):
    """A field one past its limit is refused, and no field reaches the weight above it."""
    lim = encoding.limits()
    for name in ("major", "minor", "patch", "stage", "serial"):
        args = {n: 0 for n in lim}
        args[name] = lim[name] + 1
        try:
            encoding.pack(
                args["major"],
                args["minor"],
                args["patch"],
                args["stage"],
                args["serial"],
            )
        except OverflowError:
            continue
        return False
    # Fields sorted by weight must not overlap: the span of one field (its
    # weight times its range) stays at or below the weight of the next. Reserved
    # bits between fields, as in OpenSSL's 0xMNN00PP0L, leave a gap and pass.
    ordered = sorted(encoding.fields, key=lambda field: field[1])
    for (_n, weight, limit), (_m, above, _l) in zip(ordered, ordered[1:]):
        if weight * (limit + 1) > above:
            return False
    return True


def check_compact(encoding):
    """Report only: every weight equals the span of the fields below (no gap)."""
    below = 1
    for _name, weight, limit in reversed(encoding.fields):
        if weight != below:
            return False
        below = weight * (limit + 1)
    return True


CHECKS = [
    ("order", check_order),
    ("roundtrip", check_roundtrip),
    ("signed", check_signed),
    ("guard", check_guard),
]


def semver_less(a, b):
    """SemVer 2.0.0 item 11 on strings such as 1.0.0-alpha.1 (oracle for the selftest)."""

    def parse(text):
        core, _, pre = text.partition("-")
        nums = tuple(int(x) for x in core.split("."))
        ids = pre.split(".") if pre else None
        return nums, ids

    def ident_key(item):
        return (0, int(item), "") if item.isdigit() else (1, 0, item)

    an, ai = parse(a)
    bn, bi = parse(b)
    if an != bn:
        return an < bn
    if ai is None or bi is None:
        return ai is not None and bi is None
    for x, y in zip(ai, bi):
        if x != y:
            return ident_key(x) < ident_key(y)
    return len(ai) < len(bi)


SPEC_CHAIN = [
    "1.0.0-alpha",
    "1.0.0-alpha.1",
    "1.0.0-alpha.beta",
    "1.0.0-beta",
    "1.0.0-beta.2",
    "1.0.0-beta.11",
    "1.0.0-rc.1",
    "1.0.0",
]


def oracle_ok():
    good = all(semver_less(a, b) for a, b in zip(SPEC_CHAIN, SPEC_CHAIN[1:]))
    bad = semver_less("1.0.0", "1.0.0-alpha")
    return good and not bad


# The rename chain of docs/research/version-scheme.md: strings with their rank.
CHAIN = [
    ("0.1.0-alpha", (0, 1, 0, 1)),
    ("0.1.0-beta", (0, 1, 0, 2)),
    ("0.1.0", (0, 1, 0, None)),
    ("0.1.1-alpha", (0, 1, 1, 1)),
    ("0.1.1", (0, 1, 1, None)),
    ("0.2.0-alpha", (0, 2, 0, 1)),
    ("0.2.0", (0, 2, 0, None)),
    ("1.0.0-alpha", (1, 0, 0, 1)),
    ("1.0.0", (1, 0, 0, None)),
]


def chain_values(encoding):
    out = []
    for text, (major, minor, patch, stage) in CHAIN:
        rank = encoding.final if stage is None else stage
        out.append((text, encoding.pack(major, minor, patch, rank)))
    return out


C_DECODERS = {
    "dec": """
unsigned major(unsigned e) { return e / 100000000u; }
unsigned minor(unsigned e) { return (e / 1000000u) % 100u; }
unsigned patch(unsigned e) { return (e % 1000000u) / 10u; }
unsigned stage(unsigned e) { return e % 10u; }
""",
    "7.8.12.4": """
unsigned major(unsigned e) { return e >> 24; }
unsigned minor(unsigned e) { return (e >> 16) & 0xffu; }
unsigned patch(unsigned e) { return (e >> 4) & 0xfffu; }
unsigned stage(unsigned e) { return e & 0xfu; }
""",
}
DECODER_FOR = {
    "dec-existing": "dec",
    "bin-7.8.12.4": "7.8.12.4",
}


def decoder_cost():
    """Bytes of .text and calls to the division helpers per decoder, or None."""
    gcc = shutil.which("arm-none-eabi-gcc")
    objdump = shutil.which("arm-none-eabi-objdump")
    if not gcc or not objdump:
        return None
    rows = {}
    with tempfile.TemporaryDirectory() as tmp:
        for key, source in C_DECODERS.items():
            c_file = Path(tmp) / f"{key.replace('.', '_')}.c"
            o_file = c_file.with_suffix(".o")
            c_file.write_text(source)
            subprocess.run(
                [
                    gcc,
                    "-Os",
                    "-mcpu=cortex-m0plus",
                    "-mthumb",
                    "-ffreestanding",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-c",
                    str(c_file),
                    "-o",
                    str(o_file),
                ],
                check=True,
            )
            text = subprocess.run(
                [objdump, "-h", str(o_file)], check=True, capture_output=True, text=True
            ).stdout
            size = int(re.search(r"\.text\s+([0-9a-f]+)", text).group(1), 16)
            dis = subprocess.run(
                [objdump, "-dr", str(o_file)], check=True, capture_output=True, text=True
            ).stdout
            calls = len(re.findall(r"R_ARM_THM_CALL\s+__aeabi_\w*div", dis))
            rows[key] = (size, calls)
    return rows


# Stage vocabularies the owner named (D153, D158) and one the harness uses as a
# known-bad control: Greek letters in alphabet order are not in ASCII order.
SEQUENCES = [
    ("alpha-beta-delta", ["alpha", "beta", "delta"]),
    ("alpha-beta-rc", ["alpha", "beta", "rc"]),
    ("alpha-rc", ["alpha", "rc"]),
    ("alpha-beta-gamma-delta", ["alpha", "beta", "gamma", "delta"]),
]


def initial_rank(name):
    """Stage rank from the first letter of the name: a=1 .. z=26.

    The grammar is narrower than SemVer's: a name that starts with anything but
    a lowercase ASCII letter has no rank. Uppercase letters, digits and hyphens
    sort below lowercase letters in ASCII, so a rank taken from them would
    disagree with SemVer's order.
    """
    first = name[:1]
    if not ("a" <= first <= "z"):
        raise ValueError(f"stage name {name!r} does not start with a lowercase letter")
    return ord(first) - ord("a") + 1


def sequence_agrees(names):
    """Does SemVer's ASCII order of the pre-release names equal the intended order?"""
    core = "0.1.0-"

    def compare(a, b):
        if semver_less(core + a, core + b):
            return -1
        return 1 if semver_less(core + b, core + a) else 0

    return sorted(names, key=functools.cmp_to_key(compare)) == list(names)


def initial_unique(names):
    return len({initial_rank(n) for n in names}) == len(names)


def evaluate(encoding):
    return {name: check(encoding) for name, check in CHECKS}


def selftest():
    for label, names in SEQUENCES:
        agrees = sequence_agrees(names)
        expect = label != "alpha-beta-gamma-delta"
        if agrees != expect:
            print(f"selftest: sequence {label} agrees={agrees}, expected {expect}", file=sys.stderr)
            return 1
    for bad in ("RC", "1", "-x", ""):
        try:
            initial_rank(bad)
        except ValueError:
            continue
        print(f"selftest: initial_rank accepts {bad!r}", file=sys.stderr)
        return 1
    if (initial_rank("alpha"), initial_rank("rc")) != (1, 18):
        print("selftest: initial_rank misreads a lowercase name", file=sys.stderr)
        return 1
    if not oracle_ok():
        print("selftest: the SemVer oracle misorders its own spec chain", file=sys.stderr)
        return 1
    failed = 0
    for encoding in reference_set():
        result = evaluate(encoding)
        if not all(result.values()):
            print(f"selftest: reference {encoding.name} fails {result}", file=sys.stderr)
            failed += 1
    for encoding, target in mutants():
        result = evaluate(encoding)
        if result[target]:
            print(
                f"selftest: mutant {encoding.name} passes the check {target} it must fail",
                file=sys.stderr,
            )
            failed += 1
        else:
            print(f"mutant rejected: {encoding.name} fails {target}")
    compact = {e.name: check_compact(e) for e in reference_set()}
    if compact["dec-existing"] is not True or compact["bin-gapped-0xMNN00PPS"] is not False:
        print(f"selftest: check_compact misjudges the references: {compact}", file=sys.stderr)
        failed += 1
    for first, second in (("1.0.0", "1.0.0-alpha"), ("0.2.0-alpha", "0.1.9")):
        if semver_less(first, second):
            print(f"selftest: oracle says {first} < {second}", file=sys.stderr)
            failed += 1
    if failed:
        return 1
    print("selftest: ok")
    return 0


def report():
    print("encoding\tmax\tmajor\tminor\tpatch\tstages\torder\troundtrip\tsigned\tguard\tcompact")
    for encoding in reference_set():
        lim = encoding.limits()
        result = evaluate(encoding)
        top = largest(encoding)
        print(
            "\t".join(
                [
                    encoding.name,
                    str(top),
                    str(lim["major"]),
                    str(lim["minor"]),
                    str(lim["patch"]),
                    str(lim["stage"] + 1),
                ]
                + [str(result[name]).lower() for name, _ in CHECKS]
                + [str(check_compact(encoding)).lower()]
            )
        )
    print()
    print("chain\t" + "\t".join(e.name for e in reference_set()))
    columns = [dict(chain_values(e)) for e in reference_set()]
    for text, _ in CHAIN:
        cells = []
        for column in columns:
            value = column[text]
            cells.append(f"{value} (0x{value:08x})")
        print(text + "\t" + "\t".join(cells))
    print()
    print("sequence\tASCII order equals intended\tinitial letters distinct\tinitial ranks")
    for label, names in SEQUENCES:
        ranks = ",".join(str(initial_rank(n)) for n in names)
        print(
            f"{label}\t{str(sequence_agrees(names)).lower()}\t{str(initial_unique(names)).lower()}\t{ranks}"
        )
    print()
    cost = decoder_cost()
    if cost is None:
        print("decoder cost\tnot run: arm-none-eabi-gcc (package arm-none-eabi-gcc) absent")
    else:
        print("decoder\tbytes\tdivision calls")
        for encoding_name, key in DECODER_FOR.items():
            size, calls = cost[key]
            print(f"{encoding_name}\t{size}\t{calls}")
    return 0


def main():
    if oracle_ok() is False:
        return 1
    if "--selftest" in sys.argv[1:]:
        return selftest()
    return report()


if __name__ == "__main__":
    sys.exit(main())
