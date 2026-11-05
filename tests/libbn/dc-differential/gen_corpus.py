"""Generate a seeded corpus of dc programs for the libbn/libcrypto differential run."""

import random
import sys

rng = random.Random(20261105)


def num():
    k = rng.choice([1, 1, 2, 3, 8, 20, 60])
    n = rng.randrange(0, 10**k)
    s = str(n)
    if rng.random() < 0.3:
        s = s + "." + str(rng.randrange(0, 10 ** rng.choice([1, 3, 8])))
    return ("_" if rng.random() < 0.4 else "") + s


ops = [
    "+",
    "-",
    "*",
    "/",
    "%",
    "~",
    "^",
    "v",
    "Z",
    "X",
    "p",
    "f",
    "c",
    "d",
    "r",
    "o",
    "i",
    "a",
    "P",
    "|",
    "k",
]
out = []
for _i in range(int(sys.argv[1])):
    toks = []
    for _ in range(rng.randrange(2, 12)):
        if rng.random() < 0.55:
            toks.append(num())
        else:
            op = rng.choice(ops)
            if op in ("o", "i"):
                toks += [
                    rng.choice(
                        [
                            "_2",
                            "_1",
                            "0",
                            "1",
                            "2",
                            "3",
                            "8",
                            "10",
                            "16",
                            "36",
                            "100",
                            "_100",
                            "65536",
                            "4294967297",
                        ]
                    ),
                    op,
                ]
            elif op == "k":
                toks += [rng.choice(["0", "5", "20", "_3"]), "k"]
            elif op == "|":
                toks += [num(), num(), num(), "|"]
            else:
                toks.append(op)
    toks.append("f")
    out.append(" ".join(toks))
print("\n".join(out))
