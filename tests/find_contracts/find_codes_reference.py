"""Reference implementations of the locate database format for find_contracts.

The format is the one usr.bin/find/code.c documents: a 256-byte table of the
128 ranked bigrams, then, per record, a differential count byte biased by
OFFSET -- or the escape RESET followed by one machine int -- and a residue of
printable ASCII bytes and parity-marked bigram codes, terminated by the next
record's count byte. usr.bin/find/find.c's fastfind is the in-tree reader.

"decode" runs the format backwards, so it is an oracle independent of the
encoder: a record the encoder splits, a newline it leaks into a pathname, or a
count it mis-encodes yields a pathname list that differs from the one the
encoder was given. "bigram" restates the pair listing from the same document
and pins the record boundary the reader enforces.
"""

import struct
import sys

NBIGRAM = 256
OFFSET = 14
RESET = 30


def prefix_length(s1, s2):
    """Length of the longest common prefix of two pathnames."""
    n = 0
    while n < len(s1) and n < len(s2) and s1[n] == s2[n]:
        n += 1
    return n


def bigram_stream(lines):
    """The pairs bigram(1) lists: from the common prefix on, two bytes at a time."""
    out = []
    old = " "
    for line in lines:
        start = prefix_length(old, line)
        for j in range(start, len(line) - 1, 2):
            out.append(line[j:j + 2])
        old = line
    return out


def decode(data):
    """The pathnames a coded database holds, in the order it stores them."""
    table = data[:NBIGRAM]
    bigram1 = table[0::2]
    bigram2 = table[1::2]
    body = data[NBIGRAM:]
    paths = []
    path = ""
    count = 0
    i = 0
    while i < len(body):
        c = body[i]
        i += 1
        if c == RESET:
            # putw stores one int in the machine's own order and width, and
            # the host that runs the encoder is the host that runs this.
            (word,) = struct.unpack("=i", body[i:i + struct.calcsize("=i")])
            i += struct.calcsize("=i")
            count += word - OFFSET
        else:
            count += c - OFFSET
        residue = []
        while i < len(body) and body[i] > RESET:
            b = body[i]
            i += 1
            if b < 0o200:
                residue.append(chr(b))
            else:
                k = b & 0o177
                residue.append(chr(bigram1[k]))
                residue.append(chr(bigram2[k]))
        path = path[:count] + "".join(residue)
        paths.append(path)
    return paths


def main(argv):
    if len(argv) != 2 or argv[1] not in ("bigram", "decode"):
        sys.stderr.write("usage: find_codes_reference.py bigram|decode\n")
        return 2
    if argv[1] == "bigram":
        lines = sys.stdin.read().split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        for pair in bigram_stream(lines):
            sys.stdout.write(pair + "\n")
        return 0
    for path in decode(sys.stdin.buffer.read()):
        sys.stdout.write(path + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
