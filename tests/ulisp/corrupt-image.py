#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Writes damaged copies of a saved uLisp image, one defect each.

usage: corrupt-image.py IMAGE DIRECTORY

IMAGE is an image the interpreter saved, in the word size of the program
that saved it. Each bad-NAME.img in DIRECTORY has one pointer or cell of a
kind the loader must refuse changed; images.lisp loads them all. A kind
the image lacks a cell for is an error, so the corpus cannot shrink unseen.
"""
import os
import struct
import sys

HEADER_WORDS = 6
CODESIZE = 256
ZZERO, SYMBOL, CODE, NUMBER, STREAM, ARRAY, STRING, PAIR = 0, 2, 4, 6, 8, 14, 16, 18
BUILTINS = 0xF4240000


def twist(x):
    return ((x << 2) & 0xFFFFFFFF) | ((x >> 30) & 3)


class Image:
    def __init__(self, data):
        low = struct.unpack_from("<I", data, 0)[0] - 0x31694C75
        if low not in (8, 16):
            raise SystemExit("corrupt-image: not an image: magic offset %d" % low)
        self.cell = low
        self.word = low // 2
        self.fmt = "<Q" if self.word == 8 else "<I"
        self.data = bytearray(data)
        self.base = self.get(1)
        self.cells = self.get(2)
        self.start = HEADER_WORDS * self.word + CODESIZE

    def get(self, n):
        return struct.unpack_from(self.fmt, self.data, n * self.word)[0]

    def put(self, n, value):
        struct.pack_into(self.fmt, self.data, n * self.word, value)

    def car(self, i):
        return struct.unpack_from(self.fmt, self.data, self.start + i * self.cell)[0]

    def cdr(self, i):
        return struct.unpack_from(self.fmt, self.data, self.start + i * self.cell + self.word)[0]

    def setcar(self, i, v):
        struct.pack_into(self.fmt, self.data, self.start + i * self.cell, v)

    def setcdr(self, i, v):
        struct.pack_into(self.fmt, self.data, self.start + i * self.cell + self.word, v)

    def address(self, i):
        return self.base + i * self.cell

    def index(self, address):
        return (address - self.base) // self.cell

    def cells_where(self, test):
        return [i for i in range(self.cells) if test(self.car(i), self.cdr(i))]

    def one(self, name, test):
        found = self.cells_where(test)
        if not found:
            raise SystemExit("corrupt-image: no cell for " + name)
        return found[0]


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    path, outdir = argv[1], argv[2]
    with open(path, "rb") as f:
        original = f.read()
    probe = Image(original)
    end = probe.address(probe.cells)
    wild = end + 4 * probe.cell
    cases = {}

    def case(name, edit):
        image = Image(original)
        edit(image)
        cases[name] = bytes(image.data)

    def header(n, value):
        return lambda im: im.put(n, value)

    # A string, a long symbol, an array and a pair of each kind to damage.
    def kinds(im):
        s = im.one("string", lambda a, d: a == STRING and d != 0)
        ls = im.one("long symbol", lambda a, d: a == SYMBOL and d != 0 and d & 3 == 0)
        ar = im.one("array", lambda a, d: a == ARRAY)
        pr = im.one("pair", lambda a, d: a >= PAIR and d != 0 and a <= end)
        num = im.one("number", lambda a, d: a == NUMBER)
        sym = im.one("short symbol", lambda a, d: a == SYMBOL and d & 3 != 0)
        return s, ls, ar, pr, num, sym

    s, ls, ar, pr, num, sym = kinds(probe)

    # The three arrays the image holds: a 2 by 2 matrix, a vector of five and
    # a bit array of forty bits. An array cell points at a pair, whose car is the
    # root of the tree and whose cdr is the dimension list.
    def arrays(im):
        found = {}
        for i in im.cells_where(lambda a, d: a == ARRAY):
            pair = im.index(im.cdr(i))
            dims = im.index(im.cdr(pair))
            first = im.index(im.car(dims))
            value = struct.unpack("<i", struct.pack("<I", im.cdr(first) & 0xFFFFFFFF))[0]
            rest = im.cdr(dims)
            kind = "bits" if value < 0 else ("matrix" if rest != 0 else "vector")
            found[kind] = (pair, im.index(im.car(pair)), dims, first)
        if sorted(found) != ["bits", "matrix", "vector"]:
            raise SystemExit("corrupt-image: no matrix, vector and bit array in the image")
        return found

    arr = arrays(probe)
    mat_pair, mat_root, mat_dims, mat_first = arr["matrix"]
    vec_pair, vec_root, vec_dims, vec_first = arr["vector"]
    bit_pair, bit_root, bit_dims, bit_first = arr["bits"]
    vec_kid = probe.index(probe.car(vec_root))  # the node for the first four elements
    link = probe.index(probe.cdr(s))  # the first cell of the string's chain

    case("string-head-past-end", lambda im: im.setcdr(s, end))
    case("string-head-misaligned", lambda im: im.setcdr(s, im.cdr(s) + 1))
    case("string-head-below", lambda im: im.setcdr(s, im.base - im.cell))
    case("symbol-head-wild", lambda im: im.setcdr(ls, wild))
    case("chain-loop", lambda im: im.setcar(link, im.address(link)))
    case("chain-wild", lambda im: im.setcar(link, wild))
    case("array-wild", lambda im: im.setcdr(ar, wild))
    case("array-null", lambda im: im.setcdr(ar, 0))
    # An array whose tree or dimensions do not match one another.
    case("array-dim-grown", lambda im: im.setcdr(vec_first, 100))
    case("array-dim-zero", lambda im: im.setcdr(vec_first, 0))
    case("array-dim-bit-flag", lambda im: im.setcdr(vec_first, (-5) & 0xFFFFFFFF))
    case("array-dim-second-negative", lambda im: im.setcdr(
        im.index(im.car(im.index(im.cdr(mat_dims)))), (-2) & 0xFFFFFFFF))
    case("array-dim-huge", lambda im: im.setcdr(mat_first, 65536))
    case("array-dim-not-number", lambda im: im.setcar(mat_dims, im.address(mat_pair)))
    case("array-dim-loop", lambda im: im.setcdr(mat_dims, im.address(mat_dims)))
    case("array-tree-truncated", lambda im: im.setcar(vec_root, im.address(vec_first)))
    case("array-tree-null", lambda im: im.setcar(vec_pair, 0))
    case("array-tree-loop-root", lambda im: im.setcdr(mat_root, im.address(mat_root)))
    case("array-tree-loop-deep", lambda im: im.setcar(vec_kid, im.address(vec_root)))
    case("array-tree-shared", lambda im: im.setcdr(mat_root, im.car(mat_root)))
    case("array-bits-grown", lambda im: im.setcdr(bit_first, (-4000) & 0xFFFFFFFF))
    case("array-bits-leaf-null", lambda im: im.setcar(bit_root, 0))
    case("array-bits-leaf-pair", lambda im: im.setcar(bit_root, im.address(bit_root)))
    case("pair-car-wild", lambda im: im.setcar(pr, wild))
    case("pair-car-misaligned", lambda im: im.setcar(pr, im.car(pr) + 1))
    case("pair-cdr-wild", lambda im: im.setcdr(pr, wild))
    case("pair-cdr-small", lambda im: im.setcdr(pr, 12))
    case("pair-into-chain", lambda im: im.setcdr(pr, im.address(link)))
    case("autorun-wild", header(3, wild))
    case("env-wild", header(4, wild))
    case("stack-into-chain", lambda im: im.put(5, im.address(link)))
    case("type-unknown", lambda im: im.setcar(num, 3))
    case("type-small", lambda im: im.setcar(num, 1))
    case("builtin-range", lambda im: im.setcdr(sym, twist(BUILTINS + 0x100000)))
    case("code-block", lambda im: (im.setcar(num, CODE), im.setcdr(num, 0xFFFF0000)))
    case("code-backwards", lambda im: (im.setcar(num, CODE), im.setcdr(num, 0x00100020)))
    case("stream-type", lambda im: (im.setcar(num, STREAM), im.setcdr(num, 0x0900)))
    case("stream-address", lambda im: (im.setcar(num, STREAM), im.setcdr(num, 0x0305)))
    case("base-low", header(1, 8))
    case("base-wraps", header(1, (1 << (8 * probe.word)) - probe.cell))
    case("size-over-file", header(2, probe.cells + 1))

    # Cut short: the file ends inside the last cell.
    cases["truncated"] = original[:-probe.cell // 2]

    for name, data in cases.items():
        if data == original:
            raise SystemExit("corrupt-image: %s changed nothing" % name)
        with open(os.path.join(outdir, "bad-%s.img" % name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main(sys.argv)
