"""Exact two-qubit Clifford+T statevector over Z[w], w = exp(i*pi/4).

An amplitude is (a + b*w + c*w**2 + d*w**3) / sqrt(2)**k with integer a..d,
and the four amplitudes share one k. The ring Z[w] is closed under every
gate of the Clifford+T set up to the sqrt(2) that a Hadamard introduces,
so each gate is integer addition, negation or a rotation of the four
coefficients (w**4 = -1). No float, no libm and no 64-bit divide enter,
and the exact zeros of a state stay exact zeros.

Qubit j is bit j of the amplitude index. A probability is the exact value
(u + v*sqrt(2)) / 2**k returned as (u, v); sqrt(2) is irrational, so two
probabilities are equal exactly when both integer parts agree after
cross-multiplication by the powers of two.

Coefficient width grows with the Hadamard count. In 20 seeded random
circuits per length the amplitude coefficients reached 17 bits at 1000
gates and 78 bits at 5000, and the probability coefficients 35 and 156
bits (research/two-qubit-simulation/exact_width_growth.py in
discobsd-2040-notes); Python integers absorb the growth, a fixed-width
port bounds the circuit length.

Usage: $PYTHON -c "import exact_clifford_t_2q as e; print(e.ExactState().probabilities())"
"""


def _rot(x):
    """Multiply a coefficient tuple by w."""
    a, b, c, d = x
    return (-d, a, b, c)


def _add(x, y):
    return (x[0] + y[0], x[1] + y[1], x[2] + y[2], x[3] + y[3])


def _sub(x, y):
    return (x[0] - y[0], x[1] - y[1], x[2] - y[2], x[3] - y[3])


def _neg(x):
    return (-x[0], -x[1], -x[2], -x[3])


def _mul(x, y):
    """Product in Z[w]: polynomial product reduced by w**4 = -1."""
    r = [0, 0, 0, 0]
    for i in range(4):
        for j in range(4):
            if i + j >= 4:
                r[i + j - 4] -= x[i] * y[j]
            else:
                r[i + j] += x[i] * y[j]
    return tuple(r)


SQRT2 = (0, 1, 0, -1)  # w - w**3


class ExactState:
    """The state of two qubits starting in |00>."""

    def __init__(self):
        self.v = [(1, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)]
        self.k = 0

    def _butterfly(self, j):
        for i in range(4):
            if not (i >> j) & 1:
                x, y = self.v[i], self.v[i | (1 << j)]
                self.v[i], self.v[i | (1 << j)] = _add(x, y), _sub(x, y)

    def h(self, j):
        self._butterfly(j)
        self.k += 1
        self._reduce()

    def x(self, j):
        for i in range(4):
            if not (i >> j) & 1:
                self.v[i], self.v[i | (1 << j)] = self.v[i | (1 << j)], self.v[i]

    def _phase(self, j, n):
        for i in range(4):
            if (i >> j) & 1:
                for _ in range(n % 8):
                    self.v[i] = _rot(self.v[i])

    def t(self, j):
        self._phase(j, 1)

    def tdg(self, j):
        self._phase(j, 7)

    def s(self, j):
        self._phase(j, 2)

    def z(self, j):
        self._phase(j, 4)

    def cx(self, control, target):
        for i in range(4):
            if (i >> control) & 1 and not (i >> target) & 1:
                self.v[i], self.v[i | (1 << target)] = self.v[i | (1 << target)], self.v[i]

    def cz(self):
        self.v[3] = _neg(self.v[3])

    def _reduce(self):
        """Divide the shared scale by sqrt(2) while every amplitude stays in Z[w].

        x / sqrt(2) = x * sqrt(2) / 2, so the step is legal when the product
        has only even coefficients in all four amplitudes.
        """
        while self.k > 0:
            q = [_mul(x, SQRT2) for x in self.v]
            if not all(c % 2 == 0 for y in q for c in y):
                break
            self.v = [tuple(c // 2 for c in y) for y in q]
            self.k -= 1

    def probabilities(self):
        """Return ([(u, v), ...], k): p_i = (u + v*sqrt(2)) / 2**k."""
        out = []
        for x in self.v:
            conj = (x[0], -x[3], -x[2], -x[1])
            e = _mul(x, conj)
            if e[2] != 0 or e[1] != -e[3]:
                raise ValueError("non-real norm: state is outside Z[w]")
            out.append((e[0], e[1]))
        return out, self.k

    def norm_is_one(self):
        """True when the four probabilities sum to exactly 1."""
        p, k = self.probabilities()
        return sum(u for u, _ in p) == 2**k and sum(v for _, v in p) == 0

    def max_coeff_bits(self):
        """Width in bits of the widest coefficient.

        Uses bin(); int.bit_length is absent on some MicroPython builds.
        """
        return max(len(bin(abs(c))) - 2 if c else 0 for x in self.v for c in x)


def prob_equal(p, q):
    """Exact equality of (u, v, k) probabilities (u + v*sqrt(2)) / 2**k."""
    (u1, v1, k1), (u2, v2, k2) = p, q
    return u1 * 2**k2 == u2 * 2**k1 and v1 * 2**k2 == v2 * 2**k1
