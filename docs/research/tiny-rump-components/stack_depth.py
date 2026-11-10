"""Worst-case stack bound for usr.sbin/v7fs from compiler and linker output.

frames [--callback FN=TARGET]... LISTING NAME...
    Print "NAME BYTES" for each libc function NAME from an objdump -d
    listing of the linked Thumb-1 program: the frame (four bytes per pushed
    register plus every "sub sp, #N") plus the deepest callee reached by bl
    or by a branch to another function. A register-operand "add sp", a blx
    through a register or recursion makes a function unbounded; each one is
    reported on stderr and the exit status is 1 when a NAME reaches one.
    --callback FN=TARGET resolves FN's blx to TARGET, evaluated with every
    callback edge removed, so a callback that re-enters its caller (libc
    fflush(NULL) walks the streams through _fwalk back into fflush) counts
    once.

chain SRCDIR ROOT TAKEN EXTERN CI...
    Print the deepest path from ROOT over GCC -fcallgraph-info=su files.
    Each __indirect_call edge resolves by the source text at its call site:
    the block I/O pointers reach the backend's read and write functions,
    the foreach callbacks reach every address-taken callback, and the
    byte-order pointers reach the address-taken val* functions. TAKEN lists
    the address-taken functions; EXTERN holds "NAME BYTES" lines for
    callees outside the call-graph files. SRCDIR resolves relative source
    paths in the call-site labels. A callee absent from both exits 1.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
INSN_RE = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{4}\s+){1,2}\s*(\S+)\s*(.*)$")
NODE_RE = re.compile(r'node: \{ title: "([^"]+)" label: "[^"]*?\\n[^"]*?\\n(\d+) bytes \(static\)')
DYNAMIC_RE = re.compile(r'node: \{ title: "([^"]+)" label: "[^"]*dynamic')
EDGE_RE = re.compile(
    r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)" '
    r'label: "([^":]+):(\d+)'
)
IO_TARGETS = ("fd_read", "fd_write")
CALLBACK_TARGETS = (
    "copy_block",
    "list_block",
    "lookup_by_number_subr",
    "lookup_parent_from_dir_subr",
    "lookup_subr",
    "remove_subr",
    "replace_subr",
)


def frames(argv: list[str]) -> int:
    callbacks: dict[str, str] = {}
    while argv and argv[0] == "--callback":
        fn, target = argv[1].split("=", 1)
        callbacks[fn] = target
        argv = argv[2:]
    if len(argv) < 2:
        raise SystemExit(__doc__)
    listing, names = argv[0], argv[1:]
    bodies: dict[str, list[tuple[str, str]]] = {}
    current = None
    for line in Path(listing).read_text(encoding="utf-8", errors="replace").splitlines():
        match = FUNC_RE.match(line)
        if match:
            current = match.group(2)
            bodies[current] = []
            continue
        match = INSN_RE.match(line)
        if match and current is not None:
            bodies[current].append((match.group(1), match.group(2)))
    frame: dict[str, int] = {}
    callees: dict[str, set[str]] = {}
    flaws: dict[str, str] = {}
    for name, body in bodies.items():
        size = 0
        out: set[str] = set()
        for op, args in body:
            if op == "push":
                size += 4 * len(args.strip("{}").split(","))
            elif op.startswith("sub") and args.startswith("sp, #"):
                size += int(args.split("#")[1].split()[0])
            elif op.startswith("add") and re.match(r"sp, r\d", args):
                flaws[name] = "add sp, register"
            elif op.startswith("blx") and name not in callbacks:
                flaws[name] = f"blx {args}"
            if op.startswith("b"):
                target = re.search(r"<([^>+]+)>", args)
                if target and target.group(1) != name:
                    out.add(target.group(1))
        frame[name] = size
        callees[name] = out
    memo: dict[tuple[str, bool], int] = {}
    active: set[str] = set()
    reached: set[str] = set()

    def depth(fn: str, with_callbacks: bool = True) -> int:
        key = (fn, with_callbacks)
        if key in memo:
            return memo[key]
        if fn not in frame:
            raise SystemExit(f"stack_depth: no body for {fn}")
        if fn in active:
            raise SystemExit(f"stack_depth: recursion through {fn}")
        if fn in flaws:
            reached.add(fn)
        active.add(fn)
        best = max((depth(c, with_callbacks) for c in callees[fn]), default=0)
        active.discard(fn)
        if with_callbacks and fn in callbacks:
            saved = set(active)
            active.clear()
            best = max(best, depth(callbacks[fn], False))
            active.update(saved)
        memo[key] = frame[fn] + best
        return memo[key]

    status = 0
    for name in names:
        reached.clear()
        print(name, depth(name))
        for fn in sorted(reached):
            print(f"stack_depth: {name} reaches {fn}: {flaws[fn]}", file=sys.stderr)
            status = 1
    return status


def site_group(srcdir: Path, path: str, line: int) -> str:
    source = Path(path) if Path(path).is_absolute() else srcdir / path
    lines = source.read_text(encoding="utf-8").splitlines()
    text = " ".join(lines[line - 1 : line + 2])
    if "io.read" in text or "io.write" in text:
        return "io"
    if "func(" in text:
        return "callback"
    if "VAL" in text or "conv" in text or "(*val" in text:
        return "endian"
    raise SystemExit(f"stack_depth: unclassified indirect call {path}:{line}")


def chain(srcdir: str, root: str, taken_file: str, extern_file: str, cis: list[str]) -> int:
    frame: dict[str, int] = {}
    calls: dict[str, set[str]] = {}
    for path in cis:
        text = Path(path).read_text(encoding="utf-8")
        dynamic = DYNAMIC_RE.findall(text)
        if dynamic:
            raise SystemExit(f"stack_depth: dynamic frame in {dynamic}")
        for name, size in NODE_RE.findall(text):
            frame[name] = int(size)
        for src, dst, spath, sline in EDGE_RE.findall(text):
            if dst == "__indirect_call":
                dst = "__indirect_" + site_group(Path(srcdir), spath, int(sline))
            calls.setdefault(src, set()).add(dst)
    bare: dict[str, list[str]] = {}
    for title in frame:
        bare.setdefault(title.rsplit(":", 1)[-1], []).append(title)
    taken = set(Path(taken_file).read_text(encoding="utf-8").split())
    grouped = set(IO_TARGETS) | set(CALLBACK_TARGETS)
    ungrouped = {n for n in taken if not n.startswith("val")} - grouped
    if ungrouped:
        raise SystemExit(f"stack_depth: address-taken but ungrouped: {sorted(ungrouped)}")
    members = {
        "io": [t for n in IO_TARGETS for t in bare.get(n, [])],
        "callback": [t for n in CALLBACK_TARGETS if n in taken for t in bare.get(n, [])],
        "endian": [t for n in sorted(taken) if n.startswith("val") for t in bare.get(n, [])],
    }
    extern: dict[str, int] = {}
    for line in Path(extern_file).read_text(encoding="utf-8").splitlines():
        if line.strip():
            name, size = line.split()
            extern[name] = int(size)
    memo: dict[str, tuple[int, list[str]]] = {}
    active: set[str] = set()

    def depth(fn: str) -> tuple[int, list[str]]:
        if fn in memo:
            return memo[fn]
        if fn.startswith("__indirect_"):
            group = fn[len("__indirect_") :]
            if not members[group]:
                raise SystemExit(f"stack_depth: empty indirect group {group}")
            best = max((depth(t) for t in members[group]), key=lambda r: r[0])
            return best[0], [f"({group})", *best[1]]
        if fn not in frame:
            if fn in extern:
                return extern[fn], [f"{fn}[libc {extern[fn]}]"]
            raise SystemExit(f"stack_depth: no bound for callee {fn}")
        if fn in active:
            raise SystemExit(f"stack_depth: recursion through {fn}")
        active.add(fn)
        best: tuple[int, list[str]] = (0, [])
        for callee in sorted(calls.get(fn, ())):
            result = depth(callee)
            if result[0] > best[0]:
                best = result
        active.discard(fn)
        short = fn.rsplit("/", 1)[-1]
        memo[fn] = (frame[fn] + best[0], [f"{short}[{frame[fn]}]", *best[1]])
        return memo[fn]

    total, path = depth(root)
    print(f"{root} {total}")
    print(" -> ".join(path))
    return 0


def main(argv: list[str]) -> int:
    if argv and argv[0] == "frames":
        return frames(argv[1:])
    if len(argv) >= 5 and argv[0] == "chain":
        return chain(argv[1], argv[2], argv[3], argv[4], argv[5:])
    raise SystemExit(__doc__)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
