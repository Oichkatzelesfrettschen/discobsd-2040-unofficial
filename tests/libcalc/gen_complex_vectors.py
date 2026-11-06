#!/usr/bin/env python3
"""Emit complex_vectors.h from section 5.7 of the clean-room specification.

usage: gen_complex_vectors.py SPEC.md OUTPUT.h

Every unary, binary, modulus, argument and phase line of the section becomes
one macro invocation in the header. Each expected value is cross-checked
against cmath (double precision, principal values) before it is written, so
a transcription slip in the specification or in this parser stops the
generator instead of reaching the test.

The pole-approach rows (CV_POLE) are not in the specification. They sample
atanh next to +-1 and atan next to +-i, where the real-axis formulas of a
double-precision library lose the argument to cancellation or overflow, and
take their expected values from mpmath at 60 digits, evaluated at the exact
binary value of each double argument. mpmath shares no code or formula with
calc_complex.c, and the 60-digit working precision leaves the 1e-160 offsets
well above its rounding. The generator needs mpmath for those rows only and
stops when it is missing.
"""

import cmath
import math
import re
import sys
from typing import Any, Callable

try:
    import mpmath as _mpmath  # type: ignore[import-not-found,unused-ignore]
except ImportError:  # reported by pole_rows() when the pole rows are generated
    _mpmath = None
mpmath: Any = _mpmath

UNARY: dict[str, Callable[[complex], complex]] = {
    "exp": cmath.exp,
    "log": cmath.log,
    "sqrt": cmath.sqrt,
    "sin": cmath.sin,
    "cos": cmath.cos,
    "tan": cmath.tan,
    "asin": cmath.asin,
    "acos": cmath.acos,
    "atan": cmath.atan,
    "sinh": cmath.sinh,
    "cosh": cmath.cosh,
    "tanh": cmath.tanh,
    "asinh": cmath.asinh,
    "acosh": cmath.acosh,
    "atanh": cmath.atanh,
    "conj": lambda z: z.conjugate(),
}
BINARY: dict[str, Callable[[complex, complex], complex]] = {
    "add": lambda a, b: a + b,
    "sub": lambda a, b: a - b,
    "mul": lambda a, b: a * b,
    "div": lambda a, b: a / b,
    "pow": lambda a, b: a**b,
}
PAIR = re.compile(r"\(\s*([^(),]+?)\s*,\s*([^(),]+?)\s*\)")


def num(text: str) -> float:
    return float(text.strip())


def tol_ok(got: float, want: float) -> bool:
    return abs(got - want) <= 1e-12 * max(1.0, abs(want))


def section(path: str) -> list[str]:
    lines = open(path, encoding="utf-8").read().splitlines()
    start = next(i for i, ln in enumerate(lines) if ln.startswith("### 5.7 "))
    end = next(i for i, ln in enumerate(lines) if ln.startswith("### 5.8 "))
    return lines[start:end]


POLE_OFFSETS = (1e-10, 1e-153, 1e-160)


def pole_points() -> list[tuple[str, float, float]]:
    """Arguments next to the poles of atanh (+-1) and atan (+-i), both sides."""
    pts: list[tuple[str, float, float]] = []
    for off in POLE_OFFSETS:
        for re_pole in (1.0, -1.0):
            for sign in (1.0, -1.0):
                pts.append(("atanh", re_pole, sign * off))
    for off in POLE_OFFSETS:
        for im_pole in (1.0, -1.0):
            for sign in (1.0, -1.0):
                pts.append(("atan", sign * off, im_pole))
    # Offsets along the axis of the pole itself, on the side away from the
    # branch cut (the cuts start at the poles and run outward): 1 - 1e-10.
    edge = 1.0 - 1e-10
    pts.append(("atanh", edge, 0.0))
    pts.append(("atanh", -edge, 0.0))
    pts.append(("atan", 0.0, edge))
    pts.append(("atan", 0.0, -edge))
    return pts


def pole_rows() -> list[str]:
    if mpmath is None:
        sys.exit("gen_complex_vectors: the pole rows need mpmath")
    mpmath.mp.dps = 60
    rows: list[str] = []
    for name, zr, zi in pole_points():
        z = mpmath.mpc(mpmath.mpf(zr), mpmath.mpf(zi))
        # A point on a branch cut would take its side from the sign of a float
        # zero, which an exact mpc does not carry; no row may lie on one.
        if (name == "atanh" and zi == 0.0 and abs(zr) >= 1.0) or (
            name == "atan" and zr == 0.0 and abs(zi) >= 1.0
        ):
            sys.exit("gen_complex_vectors: pole row on a branch cut")
        w = mpmath.atanh(z) if name == "atanh" else mpmath.atan(z)
        rows.append("CV_POLE(%s, %r, %r, %r, %r)" % (name, zr, zi, float(w.real), float(w.imag)))
    return rows


def main() -> None:
    spec, out = sys.argv[1], sys.argv[2]
    rows: list[str] = []
    for ln in section(spec):
        m = re.match(r"^(\w+)(?: \([^)]*\))?: (.*)$", ln)
        if not m:
            continue
        name, body = m.group(1), m.group(2)
        cases = [c.strip() for c in body.split(" ; ")]
        if name in UNARY:
            for c in cases:
                lhs, rhs = c.split("->")
                ((zr, zi),) = [(num(a), num(b)) for a, b in PAIR.findall(lhs)]
                ((er, ei),) = [(num(a), num(b)) for a, b in PAIR.findall(rhs)]
                want = UNARY[name](complex(zr, zi))
                if not (tol_ok(want.real, er) and tol_ok(want.imag, ei)):
                    sys.exit("cmath disagrees with the specification: " + ln)
                rows.append("CV_UNARY(%s, %r, %r, %r, %r)" % (name, zr, zi, er, ei))
        elif name in BINARY:
            for c in cases:
                lhs, rhs = c.split("->")
                (ar, ai), (br, bi) = [(num(a), num(b)) for a, b in PAIR.findall(lhs)]
                ((er, ei),) = [(num(a), num(b)) for a, b in PAIR.findall(rhs)]
                want = BINARY[name](complex(ar, ai), complex(br, bi))
                if not (tol_ok(want.real, er) and tol_ok(want.imag, ei)):
                    sys.exit("cmath disagrees with the specification: " + ln)
                rows.append(
                    "CV_BINARY(%s, %r, %r, %r, %r, %r, %r)" % (name, ar, ai, br, bi, er, ei)
                )
        elif name in ("abs", "arg", "phase"):
            for c in cases:
                lhs, rhs = c.split("->")
                ((zr, zi),) = [(num(a), num(b)) for a, b in PAIR.findall(lhs)]
                e = num(rhs)
                z = complex(zr, zi)
                want = {
                    "abs": abs(z),
                    "arg": cmath.phase(z),
                    "phase": math.degrees(cmath.phase(z)),
                }[name]
                if not tol_ok(want, e):
                    sys.exit("cmath disagrees with the specification: " + ln)
                rows.append("CV_SCALAR(%s, %r, %r, %r)" % (name, zr, zi, e))
    rows.extend(pole_rows())
    with open(out, "w", encoding="utf-8") as fh:
        fh.write(
            "/* Generated by gen_complex_vectors.py from section 5.7 of\n"
            " * docs/research/picomite-maths-clean-room-spec.md; the CV_POLE rows\n"
            " * come from mpmath. Do not edit. */\n"
        )
        fh.write("\n".join(rows) + "\n")
    print("gen_complex_vectors: %d vectors" % len(rows))


main()
