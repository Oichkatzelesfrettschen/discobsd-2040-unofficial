#!/usr/bin/env python3
# Line-mode tests of calc: the program reads stdin from a pipe and writes
# stdout to a pipe, so it runs without a terminal and must write no escape
# sequence. Each expression is compared with Python's float and math (or
# statistics) to 1e-12 relative; the unit pairs are compared with the NIST
# SP 811 factors to 1e-9 relative.
#
# usage: line.py "PROGRAM [ARGUMENT ...]"
#   the quoted command may carry a launcher, for example "qemu-arm ./calc-arm"

import math
import shlex
import statistics
import subprocess
import sys

checks = 0
cases = 0
failures = []


def fail(msg):
    failures.append(msg)
    print("FAIL: " + msg)


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        fail(msg)


def close(got, want, rel=1e-12, tiny=0.0):
    return abs(got - want) <= rel * abs(want) + tiny


def run(prog, text, args=()):
    r = subprocess.run(
        prog + list(args), input=text.encode(), capture_output=True, timeout=60, check=False
    )
    return r.stdout.decode(), r.returncode, r.stderr.decode()


def expected_lines(want):
    if isinstance(want, tuple) and want[0] == "raw":
        return want[1].split("\n")
    return [None]


def session(prog, table, setup=()):
    """Run (line, expectation) pairs in one process.

    An expectation is a float (relative 1e-12), ('near', value, absolute tolerance), ('err',
    text) for an error line that contains text, or ('raw', text) for the exact lines of
    text. A setup line prints one acknowledgement line, which is dropped.
    """
    global cases
    lines = list(setup) + [t[0] for t in table]
    out, rc, _ = run(prog, "\n".join(lines) + "\n")
    got = out.split("\n")
    if got and got[-1] == "":
        got.pop()
    got = got[len(setup) :]
    total = sum(len(expected_lines(t[1])) for t in table)
    check(len(got) == total, "output has %d lines, expected %d:\n%s" % (len(got), total, out))
    if len(got) != total:
        return out, rc
    check(b"\x1b" not in out.encode(), "an escape sequence reached a non-terminal")
    pos = 0
    for line, want in table:
        cases += 1
        n = len(expected_lines(want))
        text = "\n".join(got[pos : pos + n])
        pos += n
        if isinstance(want, (float, int)):
            try:
                value = float(text)
            except ValueError:
                fail("%r: expected %r, got %r" % (line, want, text))
                continue
            check(close(value, float(want)), "%r: expected %.17g, got %s" % (line, want, text))
        elif want[0] == "near":
            try:
                value = float(text)
            except ValueError:
                fail("%r: expected %r, got %r" % (line, want[1], text))
                continue
            check(
                abs(value - want[1]) <= want[2],
                "%r: expected %.17g within %g, got %s" % (line, want[1], want[2], text),
            )
        elif want[0] == "err":
            check(
                text.startswith("error:") and want[1] in text,
                "%r: expected an error containing %r, got %r" % (line, want[1], text),
            )
        else:
            check(text == want[1], "%r: expected %r, got %r" % (line, want[1], text))
    return out, rc


def num(line, want):
    return (line, want)


def err(line, text):
    return (line, ("err", text))


# Radian mode. Python's ** and unary minus follow the same precedence as calc:
# -2**2 is -4, 2**3**2 is 512.
ALG = [
    num("2+3*4", 2 + 3 * 4),
    num("(2+3)*4", (2 + 3) * 4),
    num("10-4-3", 10 - 4 - 3),
    num("100/10/5", 100 / 10 / 5),
    num("8/2*4", 8 / 2 * 4),
    num("1-2+3", 1 - 2 + 3),
    num("2*3^2", 2 * 3**2),
    num("2^3*2", 2**3 * 2),
    num("2^3^2", 2**3**2),
    num("2^-2", 2**-2),
    num("-2^2", -(2**2)),
    num("-2^-2", -(2**-2)),
    num("(-2)^2", (-2) ** 2),
    num("(-2)^3", (-2) ** 3),
    num("-1+2", -1 + 2),
    num("-2*3+4", -2 * 3 + 4),
    num("-3^2+1", -(3**2) + 1),
    num("-8/2-1", -8 / 2 - 1),
    num("--3", 3),
    num("3--3", 6),
    num("3-+3", 0),
    num("- - 4", 4),
    num("7%3", 7 % 3),
    num("-7%3", math.fmod(-7, 3)),
    num("7.5%2", 7.5 % 2),
    num("0^0", 1),
    num("2^0.5", 2**0.5),
    num("2^10", 2**10),
    num("2^-3", 2**-3),
    num("10^-5", 10**-5),
    num("1.5^2.5", 1.5**2.5),
    num("5!", math.factorial(5)),
    num("0!", 1),
    num("3!+1", 7),
    num("2^3!", 2**6),
    num("10!", math.factorial(10)),
    num("170!", float(math.factorial(170))),
    num("1e3", 1e3),
    num("1.5e-3", 1.5e-3),
    num(".5", 0.5),
    num("5.", 5.0),
    num("0x1F", 0x1F),
    num("0b101", 5),
    num("0o17", 15),
    num("1/3", 1 / 3),
    num("0.1+0.2", 0.1 + 0.2),
    num("123456789*987654321", 123456789 * 987654321),
    num("sin(0.5)", math.sin(0.5)),
    num("cos(0.5)", math.cos(0.5)),
    num("tan(0.5)", math.tan(0.5)),
    ("sin(pi)", ("near", math.sin(math.pi), 1e-15)),
    num("cos(pi)", math.cos(math.pi)),
    num("sin(pi/6)", math.sin(math.pi / 6)),
    num("asin(0.5)", math.asin(0.5)),
    num("asin(-0.9)", math.asin(-0.9)),
    num("asin(1)", math.pi / 2),
    num("acos(0.5)", math.acos(0.5)),
    num("acos(-0.75)", math.acos(-0.75)),
    num("acos(-1)", math.pi),
    num("acos(1)", 0.0),
    num("acos(0.999999)", math.acos(0.999999)),
    num("atan(0.5)", math.atan(0.5)),
    num("atan(-3)", math.atan(-3)),
    num("atan2(1,2)", math.atan2(1, 2)),
    num("atan2(-1,-1)", math.atan2(-1, -1)),
    num("atan2(1,-1)", math.atan2(1, -1)),
    num("atan2(0,-1)", math.pi),
    num("sinh(1)", math.sinh(1)),
    num("cosh(1)", math.cosh(1)),
    num("tanh(0.5)", math.tanh(0.5)),
    num("asinh(1)", math.asinh(1)),
    num("acosh(2)", math.acosh(2)),
    num("atanh(0.5)", math.atanh(0.5)),
    num("exp(1)", math.e),
    num("exp(-2.5)", math.exp(-2.5)),
    num("exp(0)", 1),
    num("ln(10)", math.log(10)),
    num("ln(0.5)", math.log(0.5)),
    num("ln(e)", 1),
    num("log10(1000)", 3),
    num("log10(2)", math.log10(2)),
    num("log2(8)", 3),
    num("log2(10)", math.log2(10)),
    num("log2(0.25)", -2),
    num("sqrt(2)", math.sqrt(2)),
    num("sqrt(144)", 12),
    num("sqrt(0)", 0),
    num("cbrt(27)", 3),
    num("cbrt(-8)", -2),
    num("cbrt(10)", 10 ** (1 / 3)),
    num("abs(-3.5)", 3.5),
    num("floor(-2.5)", -3),
    num("floor(2.5)", 2),
    num("ceil(-2.5)", -2),
    num("ceil(2.1)", 3),
    num("round(2.5)", 3),
    num("round(-2.5)", -3),
    num("round(0.49999)", 0),
    num("trunc(-2.7)", -2),
    num("frac(2.75)", 0.75),
    num("frac(-2.75)", -0.75),
    num("sign(-9)", -1),
    num("sign(0)", 0),
    num("pow(2,10)", 1024),
    num("pow(2,0.5)", 2**0.5),
    num("pow(-2,3)", -8),
    num("mod(7,3)", 1),
    num("mod(-7,3)", math.fmod(-7, 3)),
    num("hypot(3,4)", 5),
    num("hypot(5,12)", 13),
    num("hypot(1e200,1e200)", math.hypot(1e200, 1e200)),
    num("fact(10)", math.factorial(10)),
    num("fact(0)", 1),
    num("torad(180)", math.pi),
    num("todeg(pi)", 180),
    num("max(1,5,3)", 5),
    num("min(4,-2,9)", -2),
    num("sum(1,2,3.5)", 6.5),
    num("mean(1,2,3,4)", 2.5),
    num("median(3,1,2)", 2),
    num("median(4,1,3,2)", 2.5),
    num("stdev(2,4,4,4,5,5,7,9)", statistics.stdev([2, 4, 4, 4, 5, 5, 7, 9])),
    num("stdevp(2,4,4,4,5,5,7,9)", statistics.pstdev([2, 4, 4, 4, 5, 5, 7, 9])),
    num("var(1,2,3,4)", statistics.variance([1, 2, 3, 4])),
    num("varp(1,2,3,4)", statistics.pvariance([1, 2, 3, 4])),
    num("sin(cos(1))", math.sin(math.cos(1))),
    num("sqrt(3^2+4^2)", 5),
    num("2*sin(1)^2+2*cos(1)^2", 2),
    # constants: NIST CODATA 2022 (physics.nist.gov/cuu/Constants)
    num("pi", math.pi),
    num("e", math.e),
    num("c0", 299792458),
    num("planck", 6.62607015e-34),
    num("hbar", 1.054571817646156e-34),
    num("qe", 1.602176634e-19),
    num("kB", 1.380649e-23),
    num("NA", 6.02214076e23),
    num("Rgas", 8.31446261815324),
    num("sigsb", 5.670374419e-8),
    num("g0", 9.80665),
    num("Gn", 6.6743e-11),
    num("me", 9.1093837139e-31),
    num("mp", 1.67262192595e-27),
    num("eps0", 8.8541878188e-12),
    num("mu0", 1.25663706127e-6),
    num("alpha", 7.2973525643e-3),
    num("2*pi*c0", 2 * math.pi * 299792458),
    # variables, registers and ans
    num("x=5", 5),
    num("x*2", 10),
    num("y = x^2+1", 26),
    num("x = x+1", 6),
    num("m3=7", 7),
    num("m3+1", 8),
    num("ans*2", 16),
    num("ans", 16),
    num("m0", 0),
    num("m3*2", 14),
    # errors; none of them ends the session
    err("1/0", "division by zero"),
    num("ans", 14),
    err("0/0", "division by zero"),
    err("5%0", "division by zero"),
    err("mod(5,0)", "division by zero"),
    err("0^-1", "division by zero"),
    err("sqrt(-1)", "domain error"),
    err("ln(0)", "domain error"),
    err("ln(-1)", "domain error"),
    err("log10(0)", "domain error"),
    err("log2(-4)", "domain error"),
    err("asin(2)", "domain error"),
    err("acos(-1.5)", "domain error"),
    err("acosh(0.5)", "domain error"),
    err("atanh(1)", "domain error"),
    err("atanh(-2)", "domain error"),
    err("atan2(0,0)", "domain error"),
    err("fact(-1)", "domain error"),
    err("fact(1.5)", "domain error"),
    err("171!", "domain error"),
    err("(-8)^0.5", "domain error"),
    err("exp(710)", "overflow"),
    err("1e308*10", "overflow"),
    err("10^400", "overflow"),
    err("sinh(1000)", "overflow"),
    # lib/libm's exp returns the finite HUGE_VAL past its range; the power
    # must still report overflow (the tree-libm run exercises that exp)
    err("pow(1e200,2.5)", "overflow"),
    err("1e200^2.5", "overflow"),
    err("todeg(1e308)", "overflow"),
    err("tan(1e10)", "argument out of range"),
    err("tan(-1e20)", "argument out of range"),
    num("(-1)^1025", -1.0),
    num("(-1)^2000", 1.0),
    num("pow(-1,1025)", -1.0),
    num("(-1)^1e300", 1.0),
    err("(-2)^1025", "overflow"),
    err("2+", "syntax error"),
    err("(2", "syntax error"),
    err("2)", "syntax error"),
    err("*3", "syntax error"),
    err("2 3", "syntax error"),
    err("1..2", "syntax error"),
    err("0x", "syntax error"),
    err("2x", "syntax error"),
    err("foo+1", "unknown name"),
    err("foo(1)", "unknown function"),
    err("sin", "function needs arguments"),
    err("sin()", "wrong number of arguments"),
    err("sin(1,2)", "wrong number of arguments"),
    err("atan2(1)", "wrong number of arguments"),
    err("pi=3", "cannot assign"),
    err("sin=2", "cannot assign"),
    err("q1234567890123=1", "name too long"),
    err("stdev(1)", "not enough values"),
    num("x", 6),
    num("1+1", 2),
]

DEG = [
    num("sin(30)", 0.5),
    num("sin(90)", 1),
    num("sin(180)", 0),
    num("sin(270)", -1),
    num("sin(-30)", -0.5),
    num("sin(390)", 0.5),
    num("sin(1e6)", math.sin(math.radians(1e6 % 360))),
    num("cos(60)", math.cos(math.radians(60))),
    num("cos(90)", 0),
    num("cos(360)", 1),
    num("cos(180)", -1),
    num("tan(45)", math.tan(math.radians(45))),
    num("tan(180)", 0),
    err("tan(90)", "domain error"),
    err("tan(-270)", "domain error"),
    num("asin(0.5)", 30),
    num("acos(0.5)", 60),
    num("atan(1)", 45),
    num("atan2(1,1)", 45),
    num("atan2(-1,-1)", -135),
    num("sinh(1)", math.sinh(1)),
    num("torad(90)", math.pi / 2),
]

NEST = [
    num("(" * 16 + "1" + ")" * 16, 1),
    err("(" * 17 + "1" + ")" * 17, "nested too deeply"),
    num("-" * 16 + "1", 1),
    err("-" * 17 + "1", "nested too deeply"),
    num("abs(" * 16 + "1" + ")" * 16, 1),
    err("abs(" * 17 + "1" + ")" * 17, "nested too deeply"),
    num("1^" * 16 + "1", 1),
    err("1^" * 17 + "1", "nested too deeply"),
    num("((((1+2)*(3+4))))", 21),
    num("1+1", 2),
]

DIGITS = [
    ("digits 5", ("raw", "digits 5")),
    ("pi", ("raw", "3.1416")),
    ("digits 17", ("raw", "digits 17")),
    ("0.1", ("raw", "0.10000000000000001")),
    ("digits 15", ("raw", "digits 15")),
    ("0.0026194061738944669", ("raw", "0.00261940617389447")),
    ("0.000123456789012345678", ("raw", "0.000123456789012346")),
    ("-0.099999999999999999", ("raw", "-0.1")),
    ("0.00001234", ("raw", "1.234e-05")),
    ("digits 3", ("raw", "digits 3")),
    ("0.012345", ("raw", "0.0123")),
    ("99.95", ("raw", "100")),
    ("999.5", ("raw", "1e+03")),
    ("123456", ("raw", "1.23e+05")),
    ("1e100", ("raw", "1e+100")),
    ("-1.5e-100", ("raw", "-1.5e-100")),
    ("digits 15", ("raw", "digits 15")),
    ("1e15", ("raw", "1e+15")),
    ("123456789012345", ("raw", "123456789012345")),
    ("-0", ("raw", "0")),
    ("0*-1", ("raw", "0")),
    err("digits 0", "out of range"),
    err("digits", "missing number"),
]

# RPN: each line prints the top of the stack, or the result of a command.
RPN = [
    ("3 4 +", 7.0),
    ("2 *", 14.0),
    ("clear", ("raw", "stack cleared")),
    ("1 2 3", 3.0),
    ("stack", ("raw", "1: 3\n2: 2\n3: 1")),
    ("swap", 2.0),
    ("stack", ("raw", "1: 2\n2: 3\n3: 1")),
    ("dup", 2.0),
    ("stack", ("raw", "1: 2\n2: 2\n3: 3\n4: 1")),
    ("drop", 2.0),
    ("drop drop", 1.0),
    ("drop", ("raw", "empty")),
    # a sto name must be an identifier of at most 10 characters
    (
        "42 sto abcdefghijklmnopqrstuvwxyz",
        ("raw", "error: invalid name: abcdefghijklmnopqrstuvwxyz"),
    ),
    ("42 sto 9x", ("raw", "error: invalid name: 9x")),
    ("clear", ("raw", "stack cleared")),
    err("drop", "stack empty"),
    ("1 2 3 4", 4.0),
    ("roll", 3.0),
    ("stack", ("raw", "1: 3\n2: 2\n3: 1\n4: 4")),
    ("rollu", 4.0),
    ("stack", ("raw", "1: 4\n2: 3\n3: 2\n4: 1")),
    ("over", 3.0),
    ("clear", ("raw", "stack cleared")),
    ("2 3 ^", 8.0),
    ("2 10 pow", 1024.0),
    ("9 sqrt", 3.0),
    ("1 1 atan2", math.atan2(1, 1)),
    ("3 4 hypot", 5.0),
    ("5 neg", -5.0),
    ("5 !", 120.0),
    ("0xff", 255.0),
    ("-3 2 ^", 9.0),
    ("10 3 /", 10 / 3),
    ("ans", 10 / 3),
    ("clear", ("raw", "stack cleared")),
    ("10 0 /", ("err", "division by zero")),
    ("stack", ("raw", "1: 0\n2: 10")),
    ("clear", ("raw", "stack cleared")),
    err("+", "stack empty"),
    err("1 +", "stack empty"),
    ("stack", ("raw", "1: 1")),
    err("1 2 foo 3", "unknown word"),
    ("stack", ("raw", "1: 2\n2: 1\n3: 1")),
    ("clear", ("raw", "stack cleared")),
    ("1 2 3 4 sum", 10.0),
    ("clear", ("raw", "stack cleared")),
    ("1 2 3 4 5 mean", 3.0),
    ("clear", ("raw", "stack cleared")),
    ("4 1 3 2 median", 2.5),
    ("clear", ("raw", "stack cleared")),
    ("2 4 4 4 5 5 7 9 stdev", statistics.stdev([2, 4, 4, 4, 5, 5, 7, 9])),
    ("clear", ("raw", "stack cleared")),
    ("7 sto m1", 7.0),
    ("clear", ("raw", "stack cleared")),
    ("rcl m1", 7.0),
    ("5 sto k", 5.0),
    ("rcl k 2 *", 10.0),
    ("clear", ("raw", "stack cleared")),
    ("pi 2 /", math.pi / 2),
    ("sin", 1.0),
    ("clear", ("raw", "stack cleared")),
    ("1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16", 16.0),
    err("17", "stack full"),
    ("clear", ("raw", "stack cleared")),
    err("2 ln ln ln ln 0 ln", "domain error"),
]


def word_lines(value, bits):
    mask = (1 << bits) - 1
    value &= mask
    signed = value - (1 << bits) if value >> (bits - 1) else value
    dec = "dec  %d" % value + (" (%d)" % signed if value >> (bits - 1) else "")
    binary = format(value, "0%db" % bits)
    binary = " ".join(binary[i : i + 4] for i in range(0, bits, 4))
    return "hex  0x%X\n%s\noct  0o%o\nbin  %s" % (value, dec, value, binary)


def prog_cases():
    def w(expr, value, bits=32):
        return (expr, ("raw", word_lines(value, bits)))

    return [
        w("0xFF", 0xFF),
        w("255", 255),
        w("0xF0 & 0x3C", 0xF0 & 0x3C),
        w("0xF0 | 0x0F", 0xF0 | 0x0F),
        w("0xFF ^ 0x0F", 0xFF ^ 0x0F),
        w("~0", 0xFFFFFFFF),
        w("~0xF0 & 0xFF", ~0xF0 & 0xFF),
        w("1 << 31", 1 << 31),
        w("1 << 32", 0),
        w("0x80000000 >> 31", 1),
        w("0x80000000 >> 32", 0),
        w("-1", 0xFFFFFFFF),
        w("-(5)", -5),
        w("0 - 1", 0xFFFFFFFF),
        w("0xFFFFFFFF + 1", 0),
        w("0x10000 * 0x10000", 0),
        w("7 / 2", 3),
        w("7 % 4", 3),
        ("7 / 0", ("err", "division by zero")),
        ("7 % 0", ("err", "division by zero")),
        w("1 | 2 & 3", 1 | (2 & 3)),
        w("1 + 2 << 1", (1 + 2) << 1),
        w("2 * 3 + 4", 10),
        w("6 ^ 3 & 1", 6 ^ (3 & 1)),
        w("(1 + 2) * (3 + 4)", 21),
        w("0b1010", 10),
        w("0o17", 15),
        ("4294967296", ("err", "value too large")),
        ("0x1FFFFFFFF", ("err", "value too large")),
        w("rol(0x80000001, 1)", 3),
        w("ror(1, 1)", 0x80000000),
        w("rol(0x12345678, 8)", 0x34567812),
        w("ror(0x12345678, 8)", 0x78123456),
        w("rol(5, 32)", 5),
        w("(" * 16 + "1" + ")" * 16, 1),
        ("(" * 17 + "1" + ")" * 17, ("err", "nested too deeply")),
        w("-" * 16 + "1", 1),
        ("-" * 17 + "1", ("err", "nested too deeply")),
        w("~" * 16 + "1", 1),
        ("~" * 17 + "1", ("err", "nested too deeply")),
        w("a = 0x10", 0x10),
        w("a << 4", 0x100),
        w("ans + 1", 0x101),
        ("foo", ("err", "unknown name")),
        ("1 +", ("err", "syntax error")),
        ("bits 12", ("err", "bits must be")),
        ("bits 8", ("raw", "bits 8")),
        w("0xFF + 1", 0, 8),
        w("~0", 0xFF, 8),
        w("1 << 7", 0x80, 8),
        w("1 << 8", 0, 8),
        w("rol(0x81, 1)", 0x03, 8),
        w("0 - 1", 0xFF, 8),
        ("0x1FF", ("err", "value too large")),
        ("bits 16", ("raw", "bits 16")),
        w("~0", 0xFFFF, 16),
        w("0xFFFF + 2", 1, 16),
        w("0x8000 >> 15", 1, 16),
        w("0xABCD & 0xFF00", 0xAB00, 16),
        ("bits 32", ("raw", "bits 32")),
        w("~0", 0xFFFFFFFF),
    ]


# NIST SP 811 factors of every unit in the table to its coherent SI unit, written
# from the definitions (international foot 0.3048 m, avoirdupois pound
# 0.45359237 kg, standard gravity 9.80665 m/s2, US gallon 231 in3, thermochemical
# calorie 4.184 J, and so on), not copied from calc.c.
IN = 0.0254
FT = 0.3048
LB = 0.45359237
G0 = 9.80665
LBF = LB * G0
GAL = 231 * IN**3
UNITS = {
    "length": (
        "m",
        {
            "m": 1,
            "km": 1e3,
            "cm": 1e-2,
            "mm": 1e-3,
            "um": 1e-6,
            "in": IN,
            "ft": FT,
            "yd": 3 * FT,
            "mi": 5280 * FT,
            "nmi": 1852,
        },
    ),
    "mass": ("kg", {"kg": 1, "g": 1e-3, "mg": 1e-6, "t": 1e3, "lb": LB, "oz": LB / 16}),
    "volume": (
        "m3",
        {
            "m3": 1,
            "L": 1e-3,
            "mL": 1e-6,
            "galUS": GAL,
            "qtUS": GAL / 4,
            "ptUS": GAL / 8,
            "cupUS": GAL / 16,
            "flozUS": GAL / 128,
            "galUK": 4.54609e-3,
        },
    ),
    "area": (
        "m2",
        {
            "m2": 1,
            "cm2": 1e-4,
            "km2": 1e6,
            "ha": 1e4,
            "in2": IN**2,
            "ft2": FT**2,
            "acre": 43560 * FT**2,
            "mi2": (5280 * FT) ** 2,
        },
    ),
    "pressure": (
        "Pa",
        {
            "Pa": 1,
            "kPa": 1e3,
            "bar": 1e5,
            "atm": 101325,
            "psi": LBF / IN**2,
            "torr": 101325 / 760,
            "mmHg": 13595.1 * G0 * 1e-3,
        },
    ),
    "energy": (
        "J",
        {
            "J": 1,
            "kJ": 1e3,
            "cal": 4.184,
            "kcal": 4184,
            "Wh": 3600,
            "kWh": 3.6e6,
            "BTU": 1055.05585262,
            "eV": 1.602176634e-19,
            "ftlbf": FT * LBF,
        },
    ),
    "power": ("W", {"W": 1, "kW": 1e3, "hp": 550 * FT * LBF}),
    "speed": (
        "m/s",
        {"m/s": 1, "km/h": 1 / 3.6, "mph": 5280 * FT / 3600, "kn": 1852 / 3600, "ft/s": FT},
    ),
    "force": ("N", {"N": 1, "lbf": LBF, "kgf": G0}),
    "time": ("s", {"s": 1, "ms": 1e-3, "min": 60, "h": 3600, "d": 86400}),
    "angle": ("rad", {"rad": 1, "deg": math.pi / 180, "grad": math.pi / 200, "turn": 2 * math.pi}),
}
# Temperatures: kelvin = value * scale + offset.
TEMPS = {"K": (1, 0), "degC": (1, 273.15), "degF": (5 / 9, 459.67 * 5 / 9), "degR": (5 / 9, 0)}

# value, from, to, expected, written as the NIST conversion statements read
PAIRS = [
    (1, "in", "cm", 2.54),
    (1, "ft", "m", 0.3048),
    (1, "mi", "km", 1.609344),
    (1, "nmi", "m", 1852),
    (1, "lb", "kg", 0.45359237),
    (1, "oz", "g", 28.349523125),
    (1, "galUS", "L", 3.785411784),
    (1, "galUK", "L", 4.54609),
    (1, "atm", "kPa", 101.325),
    (1, "psi", "kPa", 6.894757293168),
    (1, "bar", "kPa", 100),
    (1, "kcal", "kJ", 4.184),
    (1, "kWh", "kJ", 3600),
    (1, "hp", "W", 745.6998715822702),
    (1, "mph", "m/s", 0.44704),
    (1, "kn", "km/h", 1.852),
    (1, "lbf", "N", 4.4482216152605),
    (1, "deg", "rad", math.pi / 180),
    (1, "turn", "deg", 360),
    (1, "d", "min", 1440),
    (1, "acre", "m2", 4046.8564224),
    (1, "mmHg", "Pa", 133.322387415),
    (1, "eV", "J", 1.602176634e-19),
    (1, "BTU", "J", 1055.05585262),
    (100, "degC", "degF", 212),
    (0, "degC", "K", 273.15),
    (-40, "degC", "degF", -40),
    (300, "K", "degC", 26.85),
    (0, "K", "degF", -459.67),
    (68, "degF", "degC", 20),
    (671.67, "degR", "degC", 100),
    (2 * math.pi, "rad", "deg", 360),
]


def test_units(prog):
    global cases
    table = []
    for v, a, b, want in PAIRS:
        table.append(("conv %r %s %s" % (v, a, b), want))
    for base, members in UNITS.values():
        for name, factor in members.items():
            table.append(("conv 1 %s %s" % (name, base), factor))
            table.append(("conv 2.5 %s %s" % (base, name), 2.5 / factor))
    out, rc, _ = run(prog, "\n".join(t[0] for t in table) + "\n")
    got = out.split("\n")[:-1]
    check(len(got) == len(table), "conv output has %d lines for %d inputs" % (len(got), len(table)))
    for (line, want), text in zip(table, got, strict=False):
        cases += 1
        try:
            value = float(text)
        except ValueError:
            fail("%r: got %r" % (line, text))
            continue
        # relative 1e-9; the zero results (0 degC to K is not zero, 68 degF is) carry 1e-9 absolute
        check(
            abs(value - want) <= 1e-9 * abs(want) + (1e-9 if want == 0 else 0),
            "%r: expected %.12g, got %s" % (line, want, text),
        )
    # Temperature offsets round-trip.
    for t in (-273.15, -40, 0, 36.6, 100, 1234.5):
        for a, b in (
            ("degC", "degF"),
            ("degC", "K"),
            ("degF", "K"),
            ("degC", "degR"),
            ("degF", "degR"),
        ):
            lines = ["conv %r %s %s" % (t, a, b), "conv ans %s %s" % (b, a)]
            out, rc, _ = run(prog, "\n".join(lines) + "\n")
            parts = out.split()
            cases += 1
            ok = len(parts) == 2 and abs(float(parts[1]) - t) <= 1e-9 * max(abs(t), 1)
            check(ok, "round trip %r %s->%s->%s gave %r" % (t, a, b, a, out))
    # The temperature scales against the closed forms.
    for a, (sa, oa) in TEMPS.items():
        for b, (sb, ob) in TEMPS.items():
            out, _, _ = run(prog, "conv 123.456 %s %s\n" % (a, b))
            want = (123.456 * sa + oa - ob) / sb
            cases += 1
            check(
                abs(float(out) - want) <= 1e-9 * abs(want),
                "temperature %s->%s: %r vs %r" % (a, b, out, want),
            )
    # errors, and the session continues
    table = [
        err("conv 1 foo m", "unknown unit"),
        err("conv 1 m foo", "unknown unit"),
        err("conv 1 m kg", "incompatible units"),
        err("conv 1 degC m", "incompatible units"),
        err("conv m ft", "usage"),
        err("conv", "usage"),
        err("conv 1 ft", "usage"),
        num("conv 3 * 4 ft m", 12 * 0.3048),
        num("conv 2*pi rad deg", 360),
        err("conv 1/0 m ft", "division by zero"),
        num("conv 1 m ft", 1 / 0.3048),
    ]
    session(prog, table)


STAT_VECTORS = [
    [2, 4, 4, 4, 5, 5, 7, 9],
    [1.5, 2.5, 2.5, 3.0, 10.25, -4.5, 0.125],
    [1000001, 1000002, 1000003, 1000004, 1000005.5],
]
REG_X = [1, 2, 3, 4, 5, 6]
REG_Y = [2.1, 3.9, 6.2, 7.8, 10.1, 12.2]


def test_stats(prog):
    global cases
    for vec in STAT_VECTORS:
        text = " ".join(repr(float(v)) for v in vec)
        for sep in (" ", ", "):
            out, rc, _ = run(prog, "stat " + sep.join(repr(float(v)) for v in vec) + "\n")
            got = {}
            for line in out.splitlines():
                k, v = line.split()
                got[k] = float(v)
            want = {
                "n": len(vec),
                "sum": math.fsum(vec),
                "mean": statistics.mean(vec),
                "median": statistics.median(vec),
                "min": min(vec),
                "max": max(vec),
                "var": statistics.variance(vec),
                "stdev": statistics.stdev(vec),
                "varp": statistics.pvariance(vec),
                "stdevp": statistics.pstdev(vec),
            }
            cases += 1
            check(set(got) == set(want), "stat keys %r" % sorted(got))
            for k, w in want.items():
                check(
                    k in got and close(got[k], w),
                    "stat %s of %s: %r vs %r" % (k, text, got.get(k), w),
                )
        args = ",".join(repr(float(v)) for v in vec)
        fns = {
            "mean": statistics.mean(vec),
            "median": statistics.median(vec),
            "stdev": statistics.stdev(vec),
            "var": statistics.variance(vec),
            "stdevp": statistics.pstdev(vec),
            "varp": statistics.pvariance(vec),
            "min": min(vec),
            "max": max(vec),
            "sum": math.fsum(vec),
        }
        table = [num("%s(%s)" % (k, args), w) for k, w in fns.items()]
        session(prog, table)
    # an overflowing moment prints as inf rather than a parsed garbage exponent
    out, _, _ = run(prog, "stat 1e200 2e200\n")
    cases += 1
    check("var inf\n" in out and "varp inf\n" in out and "mean 1.5e+200" in out,
          "stat of an overflowing variance: %r" % out)
    # a single value has no sample variance
    out, _, _ = run(prog, "stat 5\n")
    cases += 1
    check("var" not in out.replace("varp", "") and "n 1" in out, "stat of one value: %r" % out)
    # regression
    out, _, _ = run(
        prog,
        "reg %s ; %s\n" % (" ".join(map(repr, REG_X)), " ".join(map(repr, REG_Y))),
    )
    got = {k: float(v) for k, v in (line.split() for line in out.splitlines())}
    fit = statistics.linear_regression(REG_X, REG_Y)
    cases += 1
    check(got.get("n") == 6, "reg n: %r" % out)
    check(close(got["slope"], fit.slope), "reg slope %r vs %r" % (got["slope"], fit.slope))
    check(
        close(got["intercept"], fit.intercept),
        "reg intercept %r vs %r" % (got["intercept"], fit.intercept),
    )
    r = statistics.correlation(REG_X, REG_Y)
    check(close(got["r"], r), "reg r %r vs %r" % (got["r"], r))
    table = [
        err("reg 1 2 3", "usage"),
        err("reg 1 2 3 ; 1 2", "differ in length"),
        err("reg 1 ; 1", "not enough values"),
        err("reg 2 2 2 ; 1 2 3", "domain error"),
        err("stat 1 2 foo", "unknown name"),
        num("mean(1,2,3)", 2),
    ]
    session(prog, table)
    out, _, _ = run(prog, "stat 2*pi 1/4 sqrt(16)\n")
    cases += 1
    check("n 3" in out and close(float(out.split("\nmax ")[1].split("\n")[0]), 2 * math.pi), out)


def test_process(prog):
    global cases
    # exit status: 0, or 1 when any evaluation failed
    out, rc, _ = run(prog, "1+1\n")
    cases += 1
    check(rc == 0 and out == "2\n", "clean run: rc %r out %r" % (rc, out))
    out, rc, _ = run(prog, "1+1\n1/0\n")
    cases += 1
    check(rc == 1 and out == "2\nerror: division by zero\n", "failed run: rc %r out %r" % (rc, out))
    # the last line need not end in a newline; CR LF line ends are blanks
    out, rc, _ = run(prog, "2+2\r\n3+3")
    cases += 1
    check(out == "4\n6\n", "no final newline or CRLF: %r" % out)
    # blank lines print nothing; quit stops reading
    out, rc, _ = run(prog, "\n   \n5\nquit\n6\n")
    cases += 1
    check(out == "5\n", "blank lines and quit: %r" % out)
    # an over-long line is refused and the session goes on
    out, rc, _ = run(prog, "1" * 300 + "\n7\n")
    cases += 1
    check(out == "error: line too long\n7\n" and rc == 1, "long line: %r" % out)
    # error then ans and variables survive
    out, rc, _ = run(prog, "x=3\n2+2\n1/0\nans\nx\n")
    cases += 1
    check(out == "3\n4\nerror: division by zero\n4\n3\n", "session survives: %r" % out)
    # arguments are evaluated and the program does not read stdin
    out, rc, _ = run(prog, "", ["2+3", "sin(0)", "1/0"])
    cases += 1
    check(out == "5\n0\nerror: division by zero\n" and rc == 1, "arguments: %r" % out)
    out, rc, _ = run(prog, "", ["-d", "sin(30)"])
    cases += 1
    check(abs(float(out) - 0.5) < 1e-12, "-d: %r" % out)
    out, rc, _ = run(prog, "3 4 +\n", ["-r"])
    cases += 1
    check(out == "7\n", "-r: %r" % out)
    out, rc, _ = run(prog, "0xF0 | 0x0F\n", ["-p"])
    cases += 1
    check(out == word_lines(0xFF, 32) + "\n", "-p: %r" % out)
    out, rc, _ = run(prog, "", ["-z"])
    cases += 1
    check(rc == 2, "unknown option status %r" % rc)
    # mode and angle commands acknowledge on one line
    out, rc, _ = run(prog, "deg\nrad\nangle\nangle\nrpn\nprog\nalg\n")
    cases += 1
    check(
        out.splitlines()
        == [
            "angle degrees",
            "angle radians",
            "angle degrees",
            "angle radians",
            "mode rpn",
            "mode prog",
            "mode alg",
        ],
        "mode commands: %r" % out,
    )
    out, rc, _ = run(prog, "help\n")
    cases += 1
    check(out.startswith("calc:") and len(out.splitlines()) <= 12, "help: %r" % out)
    check(all(len(ln) <= 53 for ln in out.splitlines()), "help line wider than 53 columns")
    out, rc, _ = run(prog, "units\nconst\n")
    cases += 1
    check(all(len(ln) <= 53 for ln in out.splitlines()), "units or const line wider than 53")
    check("length:" in out and "temperature:" in out, "units list")
    out, rc, _ = run(prog, "x=1\nm2=4\nvars\n")
    cases += 1
    check(out.splitlines()[2:] == ["m2 4", "x 1"], "vars: %r" % out)
    out, rc, _ = run(prog, "x=1\nclr\nx\n")
    cases += 1
    check(out.splitlines()[1:] == ["cleared", "error: unknown name: x"], "clr: %r" % out)


def test_limits(prog):
    """Fixed table sizes: the first value past each bound is refused, the last accepted."""
    names = ["v%d" % i for i in range(17)]
    table = [num("%s=%d" % (n, i), i) for i, n in enumerate(names[:16])]
    table.append(err("%s=16" % names[16], "too many variables"))
    table.append(num("v0+v15", 15))
    table.append(num("v0=100", 100))
    session(prog, table)

    def args(n):
        return ",".join("1" for _ in range(n))

    session(
        prog,
        [
            num("sum(%s)" % args(32), 32),
            err("sum(%s)" % args(33), "too many arguments"),
            num("mean(%s)" % args(1), 1),
        ],
    )
    out, _, _ = run(prog, "stat " + " ".join("1" for _ in range(64)) + "\n")
    check(out.startswith("n 64\n"), "stat of 64 values: %r" % out[:20])
    out, _, _ = run(prog, "stat " + " ".join("1" for _ in range(65)) + "\n")
    check(out == "error: too many values\n", "stat of 65 values: %r" % out)
    out, _, _ = run(prog, "rpn\n" + "1 " * 16 + "\n" + "1\n")
    check(out.endswith("error: stack full\n"), "RPN stack bound: %r" % out[-30:])


SUITES = {
    "alg": lambda prog: session(prog, ALG),
    "deg": lambda prog: session(prog, DEG, setup=["deg"]),
    "nest": lambda prog: session(prog, NEST),
    "digits": lambda prog: session(prog, DIGITS),
    "rpn": lambda prog: session(prog, RPN, setup=["rpn"]),
    "prog": lambda prog: session(prog, prog_cases(), setup=["prog"]),
    "units": test_units,
    "stats": test_stats,
    "limits": test_limits,
    "process": test_process,
}


def main():
    if len(sys.argv) < 2:
        print("usage: line.py PROGRAM [suite ...]; suites: " + " ".join(SUITES))
        return 2
    prog = shlex.split(sys.argv[1])
    want = sys.argv[2:] or list(SUITES)
    if any(w not in SUITES for w in want):
        print("calc line: unknown suite in %r" % want)
        return 2
    for name in want:
        SUITES[name](prog)
    if failures:
        print("calc line: %d FAILED of %d cases, %d checks" % (len(failures), cases, checks))
        return 1
    print("calc line: OK (%d cases, %d checks)" % (cases, checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
