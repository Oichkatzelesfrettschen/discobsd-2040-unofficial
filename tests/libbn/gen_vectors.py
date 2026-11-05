"""Emit libbn test vectors computed with Python integers.

Usage: gen_vectors.py OUTPUT_FILE

Every value is written as signed lowercase hexadecimal ("-1f", "0"). The
random stream comes only from getrandbits on a Random seeded with a fixed
constant, so the file is byte-identical across runs and Python versions.
Division and remainder follow C: the quotient truncates toward zero and the
remainder takes the dividend's sign; each vector is checked against that
identity before it is written.

Line formats (fields are space separated):
  add A B R            sub A B R            mul A B R
  div A D Q R          divz A               exp A P R
  cmp A B C UC         bits A BITS BYTES    bit A N V
  mask A N R|ERR       rsh A N R            getword A W
  setword W            addw A W R           mulw A W R
  divw A W Q REM       dec A S              hex A S
  pred A ISZERO ISONE ISNEG
"""

import sys
from random import Random

SEED = 0x44495343  # "DISC"
LIMB = 16
BASE = 1 << LIMB
SPECIAL = (0x0000, 0x0001, 0x0002, 0x7FFE, 0x7FFF, 0x8000, 0x8001, 0xFFFE, 0xFFFF)
WORDS = (0, 1, 2, 3, 9, 10, 255, 256, 9999, 10000, 10001, 0x7FFF, 0x8000,
         0xFFFF, 0x10000, 0x10001, 0xFFFFFF, 1000000, 999999999, 1000000000,
         0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF)


def fmt(x):
    return ("-%x" % -x) if x < 0 else ("%x" % x)


def cdiv(a, d):
    """Quotient and remainder with C truncation."""
    q = abs(a) // abs(d)
    if (a < 0) != (d < 0):
        q = -q
    r = a - q * d
    assert abs(r) < abs(d) and (r == 0 or (r < 0) == (a < 0))
    assert q * d + r == a
    return q, r


class Gen:
    def __init__(self):
        self.rng = Random(SEED)
        self.lines = []

    def bits(self, n):
        return self.rng.getrandbits(n) if n > 0 else 0

    def below(self, n):
        """Uniform in 0..n-1 from getrandbits (rejection sampling)."""
        k = max(1, (n - 1).bit_length())
        while True:
            v = self.bits(k)
            if v < n:
                return v

    def sign(self, x):
        return -x if self.bits(1) else x

    def size(self):
        """Bit length drawn from a mix that favors both ends of 0..2000."""
        m = self.below(8)
        if m == 0:
            return self.below(18)
        if m == 1:
            return self.below(70)
        if m == 2:
            return 16 * self.below(40) + self.below(3) - 1
        return self.below(2001)

    def rand_int(self):
        n = self.size()
        kind = self.below(6)
        if kind == 0 and n > 0:
            return (1 << n) - 1
        if kind == 1 and n > 0:
            return 1 << (n - 1)
        if kind == 2 and n > 1:
            return (1 << (n - 1)) + 1
        return self.bits(max(n, 0))

    def limbs(self, n, top_nonzero=False):
        v = 0
        for i in range(n):
            if self.below(3) == 0:
                limb = self.bits(LIMB)
            else:
                limb = SPECIAL[self.below(len(SPECIAL))]
            v |= limb << (LIMB * i)
        if top_nonzero and n > 0 and (v >> (LIMB * (n - 1))) == 0:
            v |= SPECIAL[3 + self.below(6)] << (LIMB * (n - 1))
        return v

    def emit(self, *fields):
        self.lines.append(" ".join(str(f) for f in fields))

    def pair(self):
        if self.below(3) == 0:
            a = self.limbs(self.below(40))
            b = self.limbs(self.below(40))
        else:
            a, b = self.rand_int(), self.rand_int()
        return self.sign(a), self.sign(b)

    def add_sub_mul(self):
        smalls = range(-40, 41)
        for a in smalls:
            for b in smalls:
                self.emit("add", fmt(a), fmt(b), fmt(a + b))
                self.emit("sub", fmt(a), fmt(b), fmt(a - b))
                self.emit("mul", fmt(a), fmt(b), fmt(a * b))
        for _ in range(900):
            a, b = self.pair()
            self.emit("add", fmt(a), fmt(b), fmt(a + b))
            self.emit("sub", fmt(a), fmt(b), fmt(a - b))
            self.emit("sub", fmt(a), fmt(a), "0")
            self.emit("add", fmt(a), fmt(-a), "0")
            if len(fmt(a)) + len(fmt(b)) < 700:
                self.emit("mul", fmt(a), fmt(b), fmt(a * b))
        # Carry chains: all-ones plus one, and borrow chains.
        for n in range(1, 130):
            ones = (1 << (LIMB * n)) - 1
            self.emit("add", fmt(ones), "1", fmt(ones + 1))
            self.emit("sub", fmt(ones + 1), "1", fmt(ones))
            self.emit("sub", fmt(-ones), "1", fmt(-ones - 1))
            self.emit("mul", fmt(ones), fmt(ones), fmt(ones * ones))
            self.emit("mul", fmt(ones), fmt(-(ones + 1)), fmt(-ones * (ones + 1)))

    def division(self):
        smalls = range(-30, 31)
        for a in smalls:
            for d in smalls:
                if d == 0:
                    self.emit("divz", fmt(a))
                else:
                    q, r = cdiv(a, d)
                    self.emit("div", fmt(a), fmt(d), fmt(q), fmt(r))
        for _ in range(1500):
            a, d = self.pair()
            if d == 0:
                self.emit("divz", fmt(a))
                continue
            q, r = cdiv(a, d)
            self.emit("div", fmt(a), fmt(d), fmt(q), fmt(r))
        # Structured limb patterns: the cases that stress estimate
        # correction in Algorithm D.
        for _ in range(2500):
            nd = 1 + self.below(8)
            na = nd + self.below(6)
            d = self.sign(self.limbs(nd, True))
            a = self.sign(self.limbs(na))
            q, r = cdiv(a, d)
            self.emit("div", fmt(a), fmt(d), fmt(q), fmt(r))
        # Dividends built to land exactly on and next to multiples of d.
        for _ in range(600):
            nd = 2 + self.below(6)
            d = self.limbs(nd, True)
            k = self.rand_int() % (BASE ** self.below(6) + 1)
            for delta in (-1, 0, 1):
                a = d * k + delta
                for s in (1, -1):
                    for t in (1, -1):
                        q, r = cdiv(s * a, t * d)
                        self.emit("div", fmt(s * a), fmt(t * d), fmt(q), fmt(r))
        # Dividends whose quotient estimate needs the add-back correction.
        found = 0
        tries = 0
        while found < 300 and tries < 400000:
            tries += 1
            nd = 2 + self.below(5)
            d = self.limbs(nd, True)
            a = self.limbs(nd + 1 + self.below(4))
            if addback(a, d):
                found += 1
                q, r = cdiv(a, d)
                self.emit("div", fmt(a), fmt(d), fmt(q), fmt(r))
        self.addback_found = found

    def power(self):
        for a in range(-6, 7):
            for p in range(0, 12):
                self.emit("exp", fmt(a), fmt(p), fmt(a ** p))
        for _ in range(160):
            a = self.sign(self.bits(self.below(200)))
            p = self.below(40)
            self.emit("exp", fmt(a), fmt(p), fmt(a ** p))
        for _ in range(40):
            a = self.sign(self.bits(self.below(2001)))
            p = self.below(6)
            self.emit("exp", fmt(a), fmt(p), fmt(a ** p))
        for p in (31, 32, 33, 63, 64, 65, 100, 255, 256, 257):
            self.emit("exp", fmt(10), fmt(p), fmt(10 ** p))
            self.emit("exp", fmt(-2), fmt(p), fmt((-2) ** p))

    def compare_and_bits(self):
        for _ in range(500):
            a, b = self.pair()
            c = (a > b) - (a < b)
            uc = (abs(a) > abs(b)) - (abs(a) < abs(b))
            self.emit("cmp", fmt(a), fmt(b), c, uc)
            self.emit("cmp", fmt(a), fmt(a), 0, 0)
        for _ in range(500):
            a = self.sign(self.rand_int())
            nb = abs(a).bit_length()
            self.emit("bits", fmt(a), nb, (nb + 7) // 8)
            self.emit("pred", fmt(a), int(a == 0), int(a == 1), int(a < 0))
            for n in {0, 1, 15, 16, 17, max(nb - 1, 0), nb, nb + 1, nb + 40,
                      self.below(nb + 20), -1}:
                self.emit("bit", fmt(a), n, (abs(a) >> n) & 1 if n >= 0 else 0)
            for n in {0, 1, 7, 8, 15, 16, 17, 31, 32, self.below(nb + 1),
                      nb, nb + 1, -1}:
                if n < 0 or n > nb:
                    self.emit("mask", fmt(a), n, "ERR")
                else:
                    m = abs(a) & ((1 << n) - 1)
                    self.emit("mask", fmt(a), n, fmt(-m if a < 0 else m))
            for n in {0, 1, 7, 8, 15, 16, 17, 32, self.below(nb + 20), nb,
                      nb + 1, nb + 33}:
                m = abs(a) >> n
                self.emit("rsh", fmt(a), n, fmt(-m if a < 0 else m))
        for i in (-1, 0, 1):
            self.emit("pred", fmt(i), int(i == 0), int(i == 1), int(i < 0))

    def words(self):
        for w in WORDS:
            self.emit("setword", fmt(w))
        for _ in range(500):
            a = self.sign(self.rand_int())
            mag = abs(a)
            self.emit("getword", fmt(a), fmt(mag if mag < (1 << 32) else 0xFFFFFFFF))
        for edge in (0, 0xFFFF, 0x10000, 0xFFFFFFFF, 0x100000000, 0x100000001):
            for s in (1, -1):
                mag = edge
                self.emit("getword", fmt(s * edge),
                          fmt(mag if mag < (1 << 32) else 0xFFFFFFFF))
        for w in WORDS:
            for _ in range(24):
                a = self.sign(self.rand_int())
                self.emit("addw", fmt(a), fmt(w), fmt(a + w))
                self.emit("mulw", fmt(a), fmt(w), fmt(a * w))
                if w != 0:
                    mag = abs(a)
                    q = mag // w
                    r = mag % w
                    self.emit("divw", fmt(a), fmt(w), fmt(-q if a < 0 else q), fmt(r))
            for a in (0, 1, -1, w, -w, w - 1, w + 1, (1 << 32) - 1, 1 << 32):
                self.emit("addw", fmt(a), fmt(w), fmt(a + w))
                self.emit("mulw", fmt(a), fmt(w), fmt(a * w))
                if w != 0:
                    mag = abs(a)
                    q, r = divmod(mag, w)
                    self.emit("divw", fmt(a), fmt(w), fmt(-q if a < 0 else q), fmt(r))

    def text(self):
        for a in range(-25, 26):
            self.emit("dec", fmt(a), str(a))
        for _ in range(300):
            a = self.sign(self.rand_int())
            self.emit("dec", fmt(a), str(a))
            digits = "%X" % abs(a)
            if a == 0:
                h = "0"
            else:
                # Two hex digits per significant byte, as BN_bn2hex emits.
                h = ("-" if a < 0 else "") + ("0" if len(digits) % 2 else "") + digits
            self.emit("hex", fmt(a), h)
        for n in (1, 3, 4, 5, 8, 9, 12, 13, 100, 400, 600):
            v = 10 ** n
            for a in (v - 1, v, v + 1, -v):
                self.emit("dec", fmt(a), str(a))


def to_limbs(x, width=0):
    """Little-endian 16-bit limbs of x, zero padded to width."""
    out = []
    while x:
        out.append(x & (BASE - 1))
        x >>= LIMB
    out.extend([0] * (width - len(out)))
    return out


def addback(a, d):
    """True when Algorithm D at base 2^16 takes the add-back branch."""
    v0 = to_limbs(d)
    nd = len(v0)
    na = len(to_limbs(a))
    if nd < 2 or a < d:
        return False
    s = LIMB - v0[-1].bit_length()
    dn = d << s
    v = to_limbs(dn)
    u = to_limbs(a << s, na + 1)
    for j in range(na - nd, -1, -1):
        num = (u[j + nd] << LIMB) | u[j + nd - 1]
        qh, rh = divmod(num, v[-1])
        while qh >= BASE or qh * v[-2] > ((rh << LIMB) | u[j + nd - 2]):
            qh -= 1
            rh += v[-1]
            if rh >= BASE:
                break
        val = sum(u[j + i] << (LIMB * i) for i in range(nd + 1)) - qh * dn
        if val < 0:
            return True
        for i in range(nd + 1):
            u[j + i] = (val >> (LIMB * i)) & (BASE - 1)
    return False


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    g = Gen()
    g.add_sub_mul()
    g.division()
    g.power()
    g.compare_and_bits()
    g.words()
    g.text()
    with open(sys.argv[1], "w", encoding="ascii") as out:
        out.write("# libbn vectors seed=0x%X addback_cases=%d\n" % (SEED, g.addback_found))
        out.write("\n".join(g.lines))
        out.write("\n")
    print("gen_vectors: %d vectors, %d add-back division cases" %
          (len(g.lines), g.addback_found))
    return 0


if __name__ == "__main__":
    sys.exit(main())
