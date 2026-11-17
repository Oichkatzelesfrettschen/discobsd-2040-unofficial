# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Stack depth of the RP2040 boot ROM routines the kernel calls.

bootrom_frames.py [--boot2 BOOT2_ELF] [--check MODEL] ROM_ELF
bootrom_frames.py --self-test

ROM_ELF is a boot ROM image with symbols, such as b2.elf from the
raspberrypi/pico-bootrom-rp2040 release B2. The program prints the image's
SHA-256, the SHA-256 of its 16 KiB of ROM contents (what a readout of
addresses 0 to 0x3fff of a board gives) and its version byte, then, for
each routine the kernel reaches, the bytes of stack its deepest chain
pushes and that chain. A routine is found the way the kernel finds it:
through the function table whose address the ROM holds at 0x14, by its
two-character code, and the lookup routine through the address at 0x18.

Depths come from the disassembly. Every path through a routine is
followed from its entry address with the bytes pushed so far (four per
pushed register, the immediate of each "sub sp", less each "pop" and "add
sp"), and a call adds the callee's own deepest chain. The path line gives
each routine's own deepest point, so the figures on it sum to the total
only where each call is made from that point. A chain that meets a
register branch other than "bx lr", a register-operand change of the stack
pointer or a call cycle is reported as unbounded.

--boot2 BOOT2_ELF also measures the second-stage boot code (boot2.elf of
a kernel build), which the kernel copies to RAM and calls to restore
execute-in-place. It is entered at its first byte with a return address,
so the branch it takes only when the ROM entered it is left out.

--self-test follows a listing with a hand-computed depth and requires a
register branch, a register-operand stack adjustment, a push in a loop, a
call cycle and two disagreeing site rules to be refused.

--check MODEL compares the image with stack-model.json: every site rule
that carries a "rom" code must give the bytes measured here, and a rule
naming an unbounded or absent routine fails.

The figures hold for the image measured. Whether a board carries the same
ROM is a separate question, answered by comparing a readout of the board's
ROM with the contents hash printed here.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from stack_chain import ModelError

ROM_BYTES = 0x4000
VERSION_AT, TABLE_AT, LOOKUP_AT = 0x13, 0x14, 0x18
# The codes sys/arch/rp2040 passes to rom_func_lookup(), and the lookup
# routine itself, which the kernel calls through the word at 0x18.
CODES = ("IF", "EX", "RE", "RP", "FC", "UB")
LOOKUP = "lookup"
# The model's code for the second stage, and the label its exit branches to
# when the ROM entered it with a zero link register. The kernel's call
# carries a return address and never takes that branch.
BOOT2 = "boot2"
BOOT2_ROM_EXIT = "vector_into_flash"


LINE_RE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+){1,2}\s*(\S+)\s*(.*)$")
TARGET_RE = re.compile(r"\b([0-9a-f]+) <")
# Deeper than any ROM routine: a walk that passes it is pushing in a loop.
RUNAWAY = 4096


def instructions(listing):
    """Address-keyed (mnemonic, operands, next address) of an objdump -d listing.

    A "bx rN" within three instructions of a "pop" that names rN is
    rewritten as "bx lr": Thumb-1 code returns that way when it has
    pushed the link register.
    """
    found = []
    for line in listing.splitlines():
        match = LINE_RE.match(line)
        if match and not match.group(2).startswith("."):
            found.append([int(match.group(1), 16), match.group(2), match.group(3)])
    for index, (_, op, args) in enumerate(found):
        if op == "bx" and any(
                earlier[1] == "pop" and args.strip() in re.findall(r"r\d+", earlier[2])
                for earlier in found[max(0, index - 3):index]):
            found[index][2] = "lr"
    return {address: (op, args, following[0])
            for (address, op, args), following in zip(found, found[1:] + [(None,)])}


def deepest(entry, code, names, active=(), memo=None, ends=()):
    """Bytes and path of the deepest chain from one ROM address.

    Every path through the routine is followed with the bytes it has
    pushed so far; a call adds the callee's own deepest chain.
    """
    memo = {} if memo is None else memo
    if entry in memo:
        return memo[entry]
    if entry in active:
        raise ModelError("call cycle through " + names.get(entry, hex(entry)))
    best, seen, work = (0, []), set(), [(entry, 0)]
    while work:
        address, pushed = work.pop()
        if (address, pushed) in seen or address in ends:
            continue
        seen.add((address, pushed))
        if address not in code:
            raise ModelError(f"{address:#x} is no instruction")
        if pushed > RUNAWAY or pushed < 0:
            raise ModelError(f"stack pointer runs away at {address:#x}")
        op, args, following = code[address]
        best = max(best, (pushed, []), key=lambda item: item[0])
        target = TARGET_RE.search(args)
        target = int(target.group(1), 16) if target else None
        if op == "push":
            work.append((following, pushed + 4 * len(args.strip("{}").split(","))))
        elif op == "pop":
            if "pc" not in args:
                work.append((following, pushed - 4 * len(args.strip("{}").split(","))))
        elif op in ("sub", "add") and args.startswith("sp, #"):
            size = int(args.split("#")[1].split()[0])
            work.append((following, pushed + (size if op == "sub" else -size)))
        elif re.match(r"(sp|pc)\b", args) and op not in ("cmp", "str", "ldr", "b"):
            raise ModelError(f"{op} {args} at {address:#x}")
        elif op == "bl":
            below = deepest(target, code, names, (*active, entry), memo, ends)
            if pushed + below[0] > best[0]:
                best = (pushed + below[0], below[1])
            work.append((following, pushed))
        elif op in ("blx", "bx"):
            if args.strip() != "lr":
                raise ModelError(f"{op} {args} at {address:#x}")
        elif op in ("b", "b.n", "b.w"):
            work.append((target, pushed))
        elif op.startswith("b") and target is not None and op not in ("bics", "bic",
                                                                      "bkpt"):
            work += [(target, pushed), (following, pushed)]
        else:
            work.append((following, pushed))
    own = max(pushed for _, pushed in seen)
    memo[entry] = (best[0], [(names.get(entry, hex(entry)), own), *best[1]])
    return memo[entry]


def load(path, objdump):
    """Section bytes, code-address names and instructions of one image."""
    from elftools.elf.elffile import ELFFile

    with Path(path).open("rb") as stream:
        elf = ELFFile(stream)
        text = elf.get_section_by_name(".text")
        if text is None:
            raise ModelError(f"{path} has no .text section")
        names = {}
        for symbol in elf.get_section_by_name(".symtab").iter_symbols():
            if symbol["st_info"]["type"] == "STT_FUNC" or (
                    symbol.name and not symbol.name.startswith("$")):
                names.setdefault(symbol["st_value"] & ~1, symbol.name)
        image = (text["sh_addr"], text.data())
    listing = subprocess.run([objdump, "-d", str(path)], check=True,
                             capture_output=True, text=True).stdout
    return image, names, instructions(listing)


def routine(address, code, names, memo, ends=()):
    name = names.get(address, hex(address))
    try:
        size, chain = deepest(address, code, names, (), memo, ends)
        return {"name": name, "bytes": size, "path": chain}
    except ModelError as error:
        return {"name": name, "flaw": str(error)}


def measure(path, objdump):
    (base, rom), names, code = load(path, objdump)
    if base != 0 or len(rom) != ROM_BYTES:
        raise ModelError(f"{path} holds no 16 KiB ROM at address 0")

    def half(offset):
        return int.from_bytes(rom[offset:offset + 2], "little")

    entries, offset = {}, half(TABLE_AT)
    while half(offset):
        entries[rom[offset:offset + 2].decode("ascii")] = half(offset + 2)
        offset += 4
    entries[LOOKUP] = half(LOOKUP_AT)
    routines, memo = {}, {}
    for key in (*CODES, LOOKUP):
        if key not in entries:
            routines[key] = {"name": None, "flaw": "absent from the function table"}
        else:
            routines[key] = routine(entries[key] & ~1, code, names, memo)
    return {"image": hashlib.sha256(Path(path).read_bytes()).hexdigest(),
            "contents": hashlib.sha256(rom).hexdigest(),
            "version": rom[VERSION_AT], "routines": routines}


def measure_boot2(path, objdump):
    """The second stage, entered at its first byte with a return address."""
    (base, data), names, code = load(path, objdump)
    by_name = {name: address for address, name in names.items()}
    if BOOT2_ROM_EXIT not in by_name:
        raise ModelError(f"{path} has no {BOOT2_ROM_EXIT}")
    return {"contents": hashlib.sha256(data).hexdigest(),
            "routine": routine(base, code, names, {}, (by_name[BOOT2_ROM_EXIT],))}


def self_test():
    def listing(*lines):
        return "\n".join(f"{address:8x}:\t{word}      \t{text}"
                         for address, word, text in lines)

    good = instructions(listing(
        (0x10, "b510", "push\t{r4, lr}"), (0x12, "b082", "sub\tsp, #8"),
        (0x14, "f000", "bl\t20 <g>"), (0x18, "b002", "add\tsp, #8"),
        (0x1a, "bd10", "pop\t{r4, pc}"),
        (0x20, "b500", "push\t{lr}"), (0x22, "d001", "beq.n\t28 <g+0x8>"),
        (0x24, "b081", "sub\tsp, #4"), (0x26, "b001", "add\tsp, #4"),
        (0x28, "bc01", "pop\t{r0}"), (0x2a, "2800", "cmp\tr0, #0"),
        (0x2c, "4700", "bx\tr0")))
    # 8 pushed and 8 reserved at the call, then g's 4 pushed and 4 reserved.
    size, _ = deepest(0x10, good, {})
    if size != 24:
        raise ModelError(f"self-test: known-good listing gave {size}")
    bad = {
        "register branch": [(0x10, "b500", "push\t{lr}"), (0x12, "4718", "bx\tr3")],
        "register stack adjustment": [(0x10, "449d", "add\tsp, r3"),
                                      (0x12, "4770", "bx\tlr")],
        "push in a loop": [(0x10, "b500", "push\t{lr}"), (0x12, "e7fd", "b.n\t10 <f>")],
        "call cycle": [(0x10, "f000", "bl\t10 <f>"), (0x14, "4770", "bx\tlr")],
    }
    for label, lines in bad.items():
        try:
            deepest(0x10, instructions(listing(*lines)), {})
        except ModelError as error:
            print(f"PASS refused {label}: {error}")
        else:
            raise ModelError(f"self-test: {label} was accepted")
    routines = {"RE": {"name": "erase", "bytes": 88}}
    rule = {"function": "f", "text": "t", "rom": "RE", "bytes": 88}
    check_rules([rule], routines)
    for label, changed in (("site rule with another depth", {"bytes": 80}),
                           ("site rule naming an unmeasured routine", {"rom": "XX"})):
        try:
            check_rules([{**rule, **changed}], routines)
        except ModelError as error:
            print(f"PASS refused {label}: {error}")
        else:
            raise ModelError(f"self-test: {label} was accepted")
    print("PASS known-good listing: 24 bytes")


def check(model_path, routines):
    model = json.loads(Path(model_path).read_text(encoding="utf-8"))
    rules = [rule for rule in model["sites"] if rule.get("rom") in routines or (
        "rom" in rule and rule["rom"] != BOOT2)]
    if not rules:
        raise ModelError(f"{model_path} names none of the measured routines")
    check_rules(rules, routines)
    return len(rules)


def check_rules(rules, routines):
    for rule in rules:
        routine = routines.get(rule["rom"])
        where = f"site rule for {rule['function']} ({rule['text']})"
        if routine is None:
            raise ModelError(f"{where} names ROM code {rule['rom']}, not measured")
        if "flaw" in routine:
            raise ModelError(f"{where}: {rule['rom']} is unbounded: {routine['flaw']}")
        if rule.get("bytes") != routine["bytes"]:
            raise ModelError(f"{where} gives {rule.get('bytes')} bytes; "
                             f"{routine['name']} takes {routine['bytes']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--check", metavar="MODEL", type=Path)
    parser.add_argument("--boot2", metavar="BOOT2_ELF")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("rom_elf", nargs="?")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if not args.rom_elf:
            parser.error("ROM_ELF is required")
        result = measure(args.rom_elf, args.objdump)
        print(f"image sha256 {result['image']}")
        print(f"contents sha256 {result['contents']}")
        print(f"version {result['version']}")
        if args.boot2:
            second = measure_boot2(args.boot2, args.objdump)
            print(f"second stage contents sha256 {second['contents']}")
            result["routines"][BOOT2] = second["routine"]
        for code, found in result["routines"].items():
            if "flaw" in found:
                print(f"{code} {found['name']} unbounded: {found['flaw']}")
                continue
            print(f"{code} {found['name']} {found['bytes']}")
            print("    " + " -> ".join(f"{name}[{size}]"
                                       for name, size in found["path"]))
        if args.check:
            count = check(args.check, result["routines"])
            print(f"PASS {count} site rules of {args.check.name} agree")
        return 0
    except (ModelError, OSError, ImportError, subprocess.CalledProcessError) as error:
        print(f"bootrom_frames: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
