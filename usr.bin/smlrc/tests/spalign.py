"""Assert SP is 8-byte aligned at every call in assembly written by cgthumb.c.

AAPCS32 5.2.1.2 makes SP 8-byte aligned at every public interface, and the
Thumb-1 back end builds its argument area out of single-word pushes, so the
alignment is a property of the code generator rather than of the instruction
set. This walks each function from its entry, models every SP-moving form the
back end emits, and reports a BL or BLX reached at an offset that is not a
multiple of eight.

The model is linear: a branch target carries the SP offset of the instruction
that falls through to it. That holds because the reserve at an opening
parenthesis and the reclaim at the closing one are emitted unconditionally in
token order, and a short-circuit branch target lies inside the operand being
evaluated, so no path skips a reclaim without also skipping its reserve. Any
line that mentions SP in a form not modeled here is an error rather than a
skip, so a new emitter cannot silently pass.

A BL to a numeric label the back end marked .thumb_func is one of the two
runtime helpers GenFin writes. Those are back-end-private leaves that call
nothing and touch no doubleword, so their entry carries no public-interface
obligation and their call sites are counted separately rather than checked.
One of them, the structure pusher, returns with SP lowered by the word-rounded
size of the structure less the word it returns. A first pass walks every
function body to find which helpers have that dynamic effect; the size is then
recovered from r1 at the call, which the modeled value stack carries from the
constant the argument push wrote.
"""

import re
import sys

REG = re.compile(r"^r(\d+)$")
LABEL = re.compile(r"^([A-Za-z_.$][A-Za-z0-9_.$]*):")
NUMLABEL = re.compile(r"^\.L\d+$")
EQU = re.compile(r"^\s*\.equ\s+(\S+)\s*,\s*(\d+)")
IMM = re.compile(r"^#(-?\d+)$")
SPOFS = re.compile(r"^\[sp,\s*#(\d+)\]$")


class Fail(Exception):
    pass


def regnum(tok):
    m = REG.match(tok)
    return int(m.group(1)) if m else None


def split(line):
    """Mnemonic and operand list of an instruction line, or (None, None)."""
    body = line.split("@")[0].strip()
    if not body or body.endswith(":") or body.startswith("."):
        return None, None
    parts = body.split(None, 1)
    mnem = parts[0]
    if len(parts) == 1:
        return mnem, []
    rest = parts[1]
    if "[" in rest:
        head, _, tail = rest.partition("[")
        ops = [o.strip() for o in head.split(",") if o.strip()]
        ops.append(("[" + tail).strip())
    else:
        ops = [o.strip() for o in rest.split(",")]
    return mnem, ops


def reglist(text):
    return [r.strip() for r in text.split("{", 1)[1].split("}", 1)[0].split(",")]


def collect(lines):
    """The .equ values and the set of labels that name functions."""
    equs = {}
    funcs = set()
    thumb_func = False
    for line in lines:
        m = EQU.match(line)
        if m:
            equs[m.group(1)] = int(m.group(2))
            continue
        stripped = line.strip()
        if stripped == ".thumb_func":
            thumb_func = True
            continue
        m = LABEL.match(stripped)
        if m:
            if thumb_func:
                funcs.add(m.group(1))
            thumb_func = False
    return equs, funcs


def walk(path, lines, equs, funcs, dynamic):
    """Model one file.

    dynamic names the private helpers known to return with SP lowered by a
    runtime amount. Returns the per-function exit offsets, the checked and
    exempt call counts, and the misaligned call sites.
    """
    off = 0        # bytes SP sits below the entry SP, or None where unmodeled
    regs = {}      # register -> ("const", value) or ("sp", offset)
    vals = []      # modeled stack slots, innermost last
    exits = {}
    fails = []
    calls = 0
    exempt = 0
    name = None
    thumb_func = False

    def drop(n):
        if n >= len(vals):
            del vals[:]
        elif n > 0:
            del vals[len(vals) - n:]

    for lineno, line in enumerate(lines, 1):
        stripped = line.split("@")[0].strip()
        if stripped == ".thumb_func":
            thumb_func = True
            continue
        m = LABEL.match(stripped)
        if m:
            if thumb_func:
                off, name = 0, m.group(1)
                regs.clear()
                del vals[:]
            thumb_func = False
            continue

        mnem, ops = split(line)
        if mnem is None:
            continue

        if mnem == "push":
            rs = reglist(stripped)
            for r in rs:
                vals.append(regs.get(regnum(r)))
            if off is not None:
                off -= 4 * len(rs)
        elif mnem == "pop":
            rs = reglist(stripped)
            for r in reversed(rs):
                regs[regnum(r)] = vals.pop() if vals else None
            if off is not None:
                off += 4 * len(rs)
        elif mnem in ("add", "sub") and ops and ops[0] == "sp":
            sign = 1 if mnem == "add" else -1
            m = IMM.match(ops[-1])
            if m:
                n = int(m.group(1))
            else:
                held = regs.get(regnum(ops[-1]))
                if not held or held[0] != "const":
                    raise Fail("%s:%d: SP moved by an unknown register"
                               % (path, lineno))
                n = held[1]
            if off is not None:
                off += sign * n
            if sign > 0:
                drop(n // 4)
            else:
                vals.extend([None] * (n // 4))
        elif mnem == "mov" and ops[:1] == ["sp"]:
            held = regs.get(regnum(ops[1]))
            if not held or held[0] != "sp":
                raise Fail("%s:%d: SP restored from an untracked register"
                           % (path, lineno))
            off = held[1]
            del vals[:]
        elif mnem == "mov" and len(ops) == 2 and ops[1] == "sp":
            regs[regnum(ops[0])] = ("sp", off)
        elif mnem == "movs" and len(ops) == 2 and IMM.match(ops[1]):
            regs[regnum(ops[0])] = ("const", int(IMM.match(ops[1]).group(1)))
        elif mnem == "subs" and len(ops) == 3:
            a = regs.get(regnum(ops[1]))
            b = regs.get(regnum(ops[2]))
            if a and a[0] == "sp" and b and b[0] == "const":
                regs[regnum(ops[0])] = ("sp", None if a[1] is None
                                        else a[1] - b[1])
            elif a and a[0] == "sp":
                regs[regnum(ops[0])] = ("sp", None)
            else:
                regs.pop(regnum(ops[0]), None)
        elif mnem == "ldr" and len(ops) == 2 and ops[1].startswith("="):
            sym = ops[1][1:]
            if sym in equs:
                regs[regnum(ops[0])] = ("const", equs[sym])
            else:
                try:
                    regs[regnum(ops[0])] = ("const", int(sym, 0))
                except ValueError:
                    regs.pop(regnum(ops[0]), None)
        elif mnem == "ldr" and len(ops) == 2 and SPOFS.match(ops[1]):
            slot = int(SPOFS.match(ops[1]).group(1)) // 4
            held = vals[-1 - slot] if slot < len(vals) else None
            if held is None:
                regs.pop(regnum(ops[0]), None)
            else:
                regs[regnum(ops[0])] = held
        elif mnem in ("bl", "blx"):
            target = ops[0] if ops else ""
            private = bool(NUMLABEL.match(target)) and target in funcs
            if mnem == "bl" and NUMLABEL.match(target) and not private:
                continue                      # an unconditional jump
            if off is None:
                raise Fail("%s:%d: call reached at an unmodeled SP offset"
                           % (path, lineno))
            if private:
                exempt += 1
                if target in dynamic:
                    held = regs.get(1)
                    if not held or held[0] != "const":
                        raise Fail("%s:%d: structure size is not modeled"
                                   % (path, lineno))
                    left = ((held[1] + 3) & ~3) - 4
                    off -= left
                    vals.extend([None] * (left // 4))
            else:
                calls += 1
                if off % 8:
                    fails.append("%s:%d: %s %s reached at SP offset %d"
                                 % (path, lineno, mnem, target, off))
            for r in (0, 1, 2, 3, 12):   # AAPCS caller-saved
                regs.pop(r, None)
        elif mnem == "bx":
            if name is not None and name not in exits:
                exits[name] = off
        else:
            if any(o == "sp" for o in ops):
                raise Fail("%s:%d: unmodeled SP use: %s"
                           % (path, lineno, stripped))
            if ops:
                dst = regnum(ops[0])
                if dst is not None:
                    regs.pop(dst, None)

    return exits, calls, exempt, fails


def check(path):
    with open(path) as f:
        lines = f.readlines()
    equs, funcs = collect(lines)
    # A first pass finds the private helpers that return with SP lowered by an
    # amount known only at run time; the second models their call sites.
    exits, _, _, _ = walk(path, lines, equs, funcs, set())
    dynamic = set(n for n in funcs if exits.get(n, 0) is None)
    _, calls, exempt, fails = walk(path, lines, equs, funcs, dynamic)
    return calls, exempt, fails


def main(argv):
    total, priv, bad = 0, 0, []
    for path in argv[1:]:
        calls, exempt, fails = check(path)
        total += calls
        priv += exempt
        bad += fails
    for line in bad:
        print(line)
    if bad:
        print("spalign: %d of %d calls misaligned" % (len(bad), total))
        return 1
    print("spalign: %d calls, all at SP mod 8 == 0; "
          "%d back-end-private helper calls exempt" % (total, priv))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Fail as e:
        print(e)
        sys.exit(2)
