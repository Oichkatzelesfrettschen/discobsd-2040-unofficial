"""An independent solution counter for keen's puzzle text format.

It shares no code with keen.c: puzzles are parsed here, cages are
evaluated here, and the search is a plain permutation backtrack over
rows, so a count it agrees on with `keen --count` is evidence from two
implementations rather than one.
"""
import itertools

LABELS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def parse(text):
    n = None
    cages = []
    clues = {}
    entries = None
    lines = [line.rstrip("\n") for line in text.splitlines()]
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if not line or line.startswith("#"):
            continue
        if line.startswith("size "):
            n = int(line.split()[1])
        elif line == "cages":
            cages = lines[i:i + n]
            i += n
        elif line == "clues":
            continue
        elif line == "entries":
            entries = lines[i:i + n]
            i += n
        else:
            lab, spec = line.split()
            if spec[-1] in "+-*/":
                clues[lab] = (spec[-1], int(spec[:-1]))
            else:
                clues[lab] = (None, int(spec))
    members = {}
    for r in range(n):
        for c in range(n):
            members.setdefault(cages[r][c], []).append((r, c))
    fixed = {}
    if entries:
        for r in range(n):
            for c in range(n):
                ch = entries[r][c]
                if ch != ".":
                    fixed[(r, c)] = int(ch)
    return n, members, clues, fixed


def cage_holds(op, target, vals):
    if op is None:
        return vals[0] == target
    if op == "+":
        return sum(vals) == target
    if op == "*":
        p = 1
        for v in vals:
            p *= v
        return p == target
    a, b = sorted(vals)
    if op == "-":
        return b - a == target
    return b % a == 0 and b // a == target


def count(text, limit=10000):
    n, members, clues, fixed = parse(text)
    # Order cages by their last cell in row-major order so a cage is
    # checked as soon as its last cell is placed.
    lastrow = {}
    for lab, cells in members.items():
        last = max(cells)
        lastrow.setdefault(last[0], []).append(lab)
    grid = [[0] * n for _ in range(n)]
    found = 0

    def row_ok(r, perm):
        for c, v in enumerate(perm):
            if (r, c) in fixed and fixed[(r, c)] != v:
                return False
            for rr in range(r):
                if grid[rr][c] == v:
                    return False
        return True

    def rec(r):
        nonlocal found
        if found >= limit:
            return
        if r == n:
            found += 1
            return
        for perm in itertools.permutations(range(1, n + 1)):
            if not row_ok(r, perm):
                continue
            grid[r] = list(perm)
            ok = True
            for lab in lastrow.get(r, []):
                op, target = clues[lab]
                vals = [grid[rr][cc] for rr, cc in members[lab]]
                if not cage_holds(op, target, vals):
                    ok = False
                    break
            if ok:
                rec(r + 1)
        grid[r] = [0] * n

    rec(0)
    return found


def solutions(text, limit=10000):
    """Every satisfying grid, as tuples of rows; used to build fixtures."""
    n, members, clues, fixed = parse(text)
    out = []
    lastrow = {}
    for lab, cells in members.items():
        lastrow.setdefault(max(cells)[0], []).append(lab)
    grid = [[0] * n for _ in range(n)]

    def rec(r):
        if len(out) >= limit:
            return
        if r == n:
            out.append(tuple(tuple(row) for row in grid))
            return
        for perm in itertools.permutations(range(1, n + 1)):
            bad = False
            for c, v in enumerate(perm):
                if fixed.get((r, c), v) != v or any(grid[rr][c] == v for rr in range(r)):
                    bad = True
                    break
            if bad:
                continue
            grid[r] = list(perm)
            if all(cage_holds(*clues[lab], [grid[rr][cc] for rr, cc in members[lab]])
                   for lab in lastrow.get(r, [])):
                rec(r + 1)
        grid[r] = [0] * n

    rec(0)
    return out
