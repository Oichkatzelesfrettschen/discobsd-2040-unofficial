"""
Generate a random integer-expression program for differential testing.

Every construct is chosen so that its value is defined in C: divisors are
forced non-zero, shift counts are masked below the word width, and the
oracle is compiled with -fwrapv so that signed overflow wraps there exactly
as it does on the target. What remains is a comparison between two code
generators over the same defined program.
"""
import random
import sys

TYPES = ["int", "unsigned", "short", "char", "long"]


def expr(rnd, depth, vars):
    if depth <= 0 or rnd.random() < 0.25:
        if rnd.random() < 0.5:
            return rnd.choice(vars)
        return str(rnd.randint(-300, 300))
    op = rnd.choice(["+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^"])
    a = expr(rnd, depth - 1, vars)
    b = expr(rnd, depth - 1, vars)
    if op in ("/", "%"):
        return "(%s %s ((%s) | 1))" % (a, op, b)
    if op in ("<<", ">>"):
        return "(%s %s ((%s) & 15))" % (a, op, b)
    return "(%s %s %s)" % (a, op, b)


def cond(rnd, depth, vars):
    op = rnd.choice(["<", "<=", ">", ">=", "==", "!="])
    return "(%s %s %s)" % (expr(rnd, depth, vars), op, expr(rnd, depth, vars))


def main():
    seed = int(sys.argv[1])
    rnd = random.Random(seed)
    out = ["int printf(char *fmt, ...);", ""]
    names = []
    for i, t in enumerate(TYPES):
        out.append("%s v%d;" % (t, i))
        names.append("v%d" % i)
    out.append("")
    out.append("int")
    out.append("main(void)")
    out.append("{")
    out.append("\tint i;")
    out.append("\tint acc;")
    out.append("")
    out.append("\tacc = 0;")
    for i, t in enumerate(TYPES):
        out.append("\tv%d = %d;" % (i, rnd.randint(-100000, 100000)))
    out.append("\tfor (i = 0; i < 6; i++) {")
    for _ in range(12):
        out.append("\t\tacc = acc + (int)%s;" % expr(rnd, 3, names + ["i", "acc"]))
        out.append("\t\tif %s" % cond(rnd, 2, names + ["i", "acc"]))
        out.append("\t\t\tacc = acc - %s;" % expr(rnd, 2, names + ["i"]))
        out.append("\t\telse")
        out.append("\t\t\tacc = acc + %s;" % expr(rnd, 2, names + ["i"]))
        out.append("\t\tv%d = acc;" % rnd.randrange(len(TYPES)))
    out.append("\t\tprintf(\"%d\\n\", acc);")
    out.append("\t}")
    for i in range(len(TYPES)):
        out.append("\tprintf(\"%ld\\n\", (long)v" + str(i) + ");")
    out.append("\treturn 0;")
    out.append("}")
    print("\n".join(out))


main()
