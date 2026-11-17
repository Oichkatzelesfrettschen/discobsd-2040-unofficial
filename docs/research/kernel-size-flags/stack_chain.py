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
"""Deepest kernel call chains from GCC call-graph files and a linked kernel.

stack_chain.py [--json OUT] [--from FUNCTION]... [--from-table TABLE]...
               MODEL BUILD_DIR
    BUILD_DIR holds one kernel configuration compiled with
    -fstack-usage -fcallgraph-info=su: its *.ci files, unix.elf and the
    sources the call-site labels name relative to it. MODEL is a JSON file
    (stack-model.json) that declares what the call graph cannot say: the
    roots and their stacks, the targets of each indirect call site, the
    bounded recursions and nestings, the functions that stop on a second
    entry, the calls whose arguments keep the callee from named functions,
    the edges cut as unreachable, the callees left out by name and the
    overlays that count them. The program prints, for each root, the bytes
    of the deepest chain and its path, the same under each overlay that
    changes it, then each stack's total against its budget.

stack_chain.py --self-test
    Run the analysis over synthetic graphs: known-good graphs must give
    their hand-computed depths, and a cycle, an unresolved indirect site, a
    dynamic frame, an unknown callee, an address-taken function no entry
    names, each kind of unused model entry, a budget or count the image
    contradicts and an unknown configuration or overlay must be refused.

A function's frame is the figure GCC reports in its call-graph node. A
function without a node (assembly, libgcc, a naked handler named under
"assembly") takes the frame its linked disassembly shows: four bytes per
pushed register plus every immediate "sub sp". A register-operand stack
adjustment or an indirect branch in such a function is refused. Functions
absent from the linked image are dropped, since --gc-sections removed them.

A table site reads its targets from the linked image: the model names the
table symbol, the structure and the member, DWARF gives the member offset
and the element size, and the words stored there name the functions. A
table without a structure is an array of function pointers, and one marked
"pointers" is an array of pointers to the structure. A site's "targets"
list names functions assigned at run time; the ones absent from the image
drop out, so one model serves configurations that link different drivers.
"only" keeps the table targets whose names start with one of its prefixes
and "except" removes targets by name; both narrow a table shared by device
classes to the class the site's device number can name. "frames" lists
functions the call passes through without reaching their other callees,
and the site then costs the sum of their frames; a list in place of a name
costs the largest frame among its linked members.

A site's "bytes" is the depth of a callee outside the compiled image,
taken from another measurement (bootrom_frames.py for the boot ROM).

A "nesting" entry bounds a cycle that passes through several functions:
its function may be on a chain "depth" times. With "cut" the bound itself
ends the cycle and must be reached; without it the graph must end sooner,
and a further entry is refused. "then_without" bars named functions under
every entry after the first, and "elements" with "struct" holds the depth
to the element count of an array in the image.

An overlay is a second bound with calls counted that the plain bound
leaves out: an "excluded" callee marked with the overlay's name, bounded
by its "on_top" nesting rule, a site's "left_out" targets and a marked
"nesting" entry. Each overlay is reported beside the plain figures. Its
stack total counts every chain with the overlay; an overlay marked "once"
enters its callee afresh in one chain only and takes the other handlers
as running after that entry.

A model that lists "configs" serves several kernels: an entry with its own
"configs" list applies to those alone, and the configuration is the build
directory's name. A stack's budget is held to the image: "top", "base"
and "struct" give the span between two symbols less a structure, and
"object" the size of an array. A root may run on several stacks, add the
bytes its assembly entry pushes ("pushed"), or have no compiled entry at
all (a null "entry": the handler leaves this stack before it calls).

A guard bars named functions under one caller's calls to one callee, where
the arguments rule them out. Its "require" pattern must match the source
text of every such call, so a build that inlines a different call into the
caller, or an edit that changes an argument, is refused.

Every linked function whose address an object file stores (an R_ARM_ABS32
relocation) must be a site's target, a root's entry or an "exempt" entry.
A callback registered in a file the model's author did not read is caught
there, since its address is taken and no site names it.

The result is a bound under the model, not a measurement. It leaves out
what the model's "excluded" entries name, and it adds every interrupt
level's deepest handler on top of the deepest process chain whether or not
the kernel masks that level there.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

NODE_RE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE_RE = re.compile(
    r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"'
    r'(?: label: "([^":]+):(\d+):\d+")?'
)
FRAME_RE = re.compile(r"(\d+) bytes \(([a-z,]+)\)")
FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
INSN_RE = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{4}\s+){1,2}\s*(\S+)\s*(.*)$")
# r0-r3, r12, lr, pc and xPSR, plus the word that aligns the frame to eight
# bytes when the interrupted stack pointer is not (ARMv6-M ARM B1.5.6, B1.5.7).
EXCEPTION_FRAME = 32 + 4
INDIRECT = "__indirect_call"


class ModelError(ValueError):
    pass


def bare(title):
    return title.rsplit(":", 1)[-1]


def load_graph(texts):
    """Return frames, edges and indirect sites from call-graph file texts."""
    frames, edges, sites, places = {}, {}, [], {}
    for text in texts:
        for title, label in NODE_RE.findall(text):
            match = FRAME_RE.search(label)
            if not match:
                continue
            if match.group(2) != "static":
                raise ModelError(f"{bare(title)}: {match.group(2)} frame")
            frames[title] = int(match.group(1))
        for source, target, path, line in EDGE_RE.findall(text):
            if target == INDIRECT:
                sites.append((source, path, int(line)))
            else:
                edges.setdefault(source, set()).add(target)
                if path:
                    places.setdefault((bare(source), bare(target)), set()).add(
                        (path, int(line)))
    return frames, edges, sites, places


def disassembly_frames(listing):
    """Frame, callees and refusal reason of each function in objdump -d text."""
    result, current, popped = {}, None, ""
    for line in listing.splitlines():
        match = FUNC_RE.match(line)
        if match:
            current = match.group(2)
            result[current] = {"frame": 0, "calls": set(), "flaw": None}
            continue
        match = INSN_RE.match(line)
        if not match or current is None:
            continue
        op, args = match.groups()
        entry = result[current]
        if op == "push":
            entry["frame"] += 4 * len(args.strip("{}").split(","))
        elif op.startswith("sub") and args.startswith("sp, #"):
            entry["frame"] += int(args.split("#")[1].split()[0])
        elif op.startswith(("add", "sub", "mov")) and re.match(r"sp, r\d", args):
            entry["flaw"] = f"{op} {args}"
        elif op == "blx" or (op == "bx" and not args.startswith("lr") and
                             args.split()[0] not in popped):
            # Thumb-1 returns through "pop {..., rN}; bx rN"; any other
            # register branch has a target the listing does not name.
            entry["flaw"] = entry["flaw"] or f"{op} {args}"
        if op == "pop":
            popped = re.findall(r"r\d+", args)
        elif op.startswith("bl"):
            popped = []
        else:
            # The first operand is the register an instruction writes.
            popped = [name for name in popped if name != args.split(",")[0].strip()]
        if op.startswith("b") and op not in ("bx", "blx", "bics", "bic", "bkpt"):
            target = re.search(r"<([^>+]+)>", args)
            if target and target.group(1) != current:
                entry["calls"].add(target.group(1))
    return result


class Image:
    """Symbols, bytes and structure layouts of the linked kernel."""

    def __init__(self, path, objdump):
        try:
            from elftools.elf.elffile import ELFFile
        except ImportError as error:
            raise ModelError("not run: pyelftools is absent (pip package "
                             "pyelftools, python-pyelftools on Arch)") from error
        self.stream = Path(path).open("rb")
        self.elf = ELFFile(self.stream)
        self.symbols, self.functions, self.by_address = {}, set(), {}
        self.code, self.addresses = set(), {}
        for symbol in self.elf.get_section_by_name(".symtab").iter_symbols():
            kind = symbol["st_info"]["type"]
            if symbol.name:
                self.addresses[symbol.name] = symbol["st_value"]
            if kind == "STT_FUNC":
                self.functions.add(symbol.name)
                self.code.add(symbol.name)
                self.by_address.setdefault(symbol["st_value"] & ~1, symbol.name)
            if kind in ("STT_FUNC", "STT_OBJECT"):
                self.symbols[symbol.name] = (symbol["st_value"], symbol["st_size"])
        listing = subprocess.run([objdump, "-d", str(path)], check=True,
                                 capture_output=True, text=True).stdout
        self.listing = disassembly_frames(listing)
        # objdump heads a function with one of the names at its address;
        # the other names, such as __aeabi_uidiv beside __udivsi3, share it.
        heads = {int(match.group(1), 16): match.group(2)
                 for match in map(FUNC_RE.match, listing.splitlines()) if match}
        for name in sorted(self.functions - self.listing.keys()):
            head = heads.get(self.symbols[name][0] & ~1)
            if head is not None:
                self.listing[name] = self.listing[head]
        self.functions.update(self.listing)
        self.structs = None

    def same_address(self, name):
        """Every function name the image places at this function's address."""
        address = self.symbols[name][0]
        return {other for other in self.code if self.symbols[other][0] == address}

    def read(self, address, size):
        for segment in self.elf.iter_segments():
            if segment["p_type"] != "PT_LOAD":
                continue
            start = segment["p_vaddr"]
            if start <= address and address + size <= start + segment["p_filesz"]:
                return segment.data()[address - start:address - start + size]
        raise ModelError(f"address {address:#x} has no bytes in the image")

    def struct(self, name):
        if self.structs is None:
            self.structs = {}
            for unit in self.elf.get_dwarf_info().iter_CUs():
                for die in unit.iter_DIEs():
                    if (die.tag != "DW_TAG_structure_type" or
                            "DW_AT_byte_size" not in die.attributes or
                            "DW_AT_name" not in die.attributes):
                        continue
                    members = {
                        child.attributes["DW_AT_name"].value.decode():
                        child.attributes["DW_AT_data_member_location"].value
                        for child in die.iter_children()
                        if child.tag == "DW_TAG_member" and
                        "DW_AT_name" in child.attributes and
                        "DW_AT_data_member_location" in child.attributes
                    }
                    layout = (die.attributes["DW_AT_byte_size"].value, members)
                    key = die.attributes["DW_AT_name"].value.decode()
                    known = self.structs.setdefault(key, [])
                    if layout not in known:
                        known.append(layout)
        if name not in self.structs:
            raise ModelError(f"struct {name} is absent from the debug information")
        if len(self.structs[name]) != 1:
            raise ModelError(f"struct {name} has {len(self.structs[name])} layouts")
        return self.structs[name][0]

    def table_targets(self, rule):
        """Functions stored in one member of every element of a table."""
        if "struct" in rule:
            size, members = self.struct(rule["struct"])
            if rule["member"] not in members:
                raise ModelError(
                    f"struct {rule['struct']} has no member {rule['member']}")
            member = members[rule["member"]]
        else:
            size, member = 4, 0
        if rule["table"] not in self.symbols:
            raise ModelError(f"table {rule['table']} is absent from the image")
        address, length = self.symbols[rule["table"]]
        step = 4 if rule.get("pointers") else size
        if length == 0 or length % step:
            raise ModelError(f"table {rule['table']} is not whole elements")
        targets = set()
        for offset in range(0, length, step):
            element = address + offset
            if rule.get("pointers"):
                element = int.from_bytes(self.read(element, 4), "little")
                if element == 0:
                    continue
            word = int.from_bytes(self.read(element + member, 4), "little")
            if word == 0:
                continue
            if word & ~1 not in self.by_address:
                raise ModelError(f"{rule['table']}+{offset}: {word:#x} is no function")
            targets.add(self.by_address[word & ~1])
        return targets


def address_taken(build, functions, objdump):
    """Linked functions whose address an object file stores in a word."""
    readelf = str(Path(objdump).with_name(Path(objdump).name.replace("objdump",
                                                                    "readelf")))
    names = set()
    for path in sorted(build.glob("*.o")):
        text = subprocess.run([readelf, "-rW", str(path)], check=True,
                              capture_output=True, text=True).stdout
        skip = False
        for line in text.splitlines():
            if line.startswith("Relocation section"):
                # Debug and unwind sections name every function they describe.
                skip = ".rel.debug" in line or ".rel.ARM" in line
            fields = line.split()
            if not skip and len(fields) >= 5 and fields[2] == "R_ARM_ABS32":
                # A static function is named through its section's symbol.
                names.add(fields[4].removeprefix(".text."))
    return names & functions


def analyze(frames, edges, sites, model, live, listing, table_targets, source_line,
            taken=(), places=None):
    """Resolve the graph under the model and return each root's deepest chain."""
    titles = {}
    for title in frames:
        if bare(title) in live:
            titles.setdefault(bare(title), []).append(title)
    assembly = set(model.get("assembly", ()))
    excluded = {entry["callee"]: entry["reason"] for entry in model.get("excluded", ())}
    recursion = {entry["function"]: entry for entry in model.get("recursion", ())}
    cuts = {(entry["caller"], entry["callee"]): 0 for entry in model.get("cuts", ())}
    reentry = {entry["function"]: 0 for entry in model.get("reentry", ())}
    guards = {(entry["caller"], entry["callee"]): [frozenset(entry["without"]), 0]
              for entry in model.get("guards", ())}
    nesting = {entry["function"]: entry for entry in model.get("nesting", ())}
    again, stopped, fixed = dict.fromkeys(nesting, 0), dict.fromkeys(nesting, 0), {}
    rules = model.get("sites", ())
    used = [0] * len(rules)
    calls, flat = {}, {}
    covered = {root["entry"] for root in model["roots"]} - {None}
    covered.update(entry["function"] for entry in model.get("exempt", ()))
    for title, targets in edges.items():
        if bare(title) in live:
            calls.setdefault(title, set()).update(targets)
    for source, path, line in sites:
        if bare(source) not in live:
            continue
        text = source_line(path, line)
        matches = [index for index, rule in enumerate(rules)
                   if rule["function"] == bare(source) and rule["text"] in text]
        if len(matches) != 1:
            raise ModelError(f"indirect call in {bare(source)} at {path}:{line} "
                             f"matches {len(matches)} site rules: {text.strip()}")
        rule = rules[matches[0]]
        used[matches[0]] += 1
        if "excluded" in rule:
            continue
        if "bytes" in rule:
            # The callee lies outside the compiled image; the rule carries
            # the depth another measurement gave it.
            name = f"{bare(source)} {rule['text']}"
            fixed[name] = rule["bytes"]
            calls.setdefault(source, set()).add(name)
            continue
        if "frames" in rule:
            # The call reaches these functions and returns through them
            # without their other callees: the site costs their frames.
            name = f"{bare(source)} site {path.rsplit('/', 1)[-1]}:{line}"
            flat[name] = [[item] if isinstance(item, str) else item
                          for item in rule["frames"]]
            covered.update(item for group in flat[name] for item in group)
            calls.setdefault(source, set()).add(name)
            continue
        targets = set(rule.get("targets", ())) & live
        if "table" in rule:
            targets |= table_targets(rule)
        if "only" in rule:
            targets = {name for name in targets if name.startswith(tuple(rule["only"]))}
        targets -= set(rule.get("except", ()))
        covered.update(targets)
        if not targets and "table" not in rule:
            raise ModelError(f"site rule for {bare(source)} ({rule['text']}) "
                             "names no target")
        calls.setdefault(source, set()).update(targets)
    for index, rule in enumerate(rules):
        if not used[index]:
            raise ModelError(f"site rule for {rule['function']} ({rule['text']}) "
                             "matches no call site")

    def node(name, caller):
        """The graph node a callee name denotes, seen from its caller."""
        if name in frames:
            return name
        local = caller.rsplit(":", 1)[0] + ":" + name if ":" in caller else None
        if local in frames:
            return local
        found = titles.get(name, [])
        if len(found) == 1:
            return found[0]
        if len(found) > 1:
            raise ModelError(f"{name} names {len(found)} static functions")
        return name

    # A name is covered when any function at its address is: the vector
    # table's unused entries are weak names for one default handler.
    groups = taken if isinstance(taken, dict) else {name: {name} for name in taken}
    stray = sorted(name for name, group in groups.items() if not group & covered)
    if stray:
        raise ModelError("address-taken functions no site rule, root or exemption "
                         "names: " + ", ".join(stray))

    for entry in model.get("guards", ()):
        # A guard rests on the arguments at every call the caller makes, so
        # each call's source text must show them.
        for path, line in sorted((places or {}).get(
                (entry["caller"], entry["callee"]), ())) if "require" in entry else ():
            text = source_line(path, line) + " " + source_line(path, line + 1)
            if not re.search(entry["require"], text):
                raise ModelError(f"guard {entry['caller']} -> {entry['callee']} does "
                                 f"not hold at {path}:{line}: {text.strip()}")

    memo, active, banned = {}, [], []

    def shape(title):
        """A function's own frame and the callees its body names."""
        name = bare(title)
        if title in fixed:
            return fixed[title], set()
        if title in flat:
            # One frame per group: the largest among the functions linked.
            return sum(max((shape(node(item, title))[0] for item in group
                            if item in live), default=0)
                       for group in flat[title]), set()
        if title in frames and name not in assembly:
            return frames[title], calls.get(title, set())
        if name not in listing:
            raise ModelError(f"no frame for callee {name}")
        entry = listing[name]
        if entry["flaw"]:
            raise ModelError(f"{name}: disassembly has {entry['flaw']}")
        return entry["frame"], set(entry["calls"]) | calls.get(title, set())

    def depth(title):
        name = bare(title)
        frame, callees = shape(title)
        if title in active and name in reentry:
            reentry[name] += 1
            return (frame, [(name + " (re-entered)", frame)])
        later = None
        if name in nesting:
            entered = active.count(title) + nesting[name].get("entered", 0)
            if entered >= nesting[name]["depth"]:
                # The bound's reason says this entry cannot happen.
                stopped[name] += 1
                return (0, [])
            if entered:
                again[name] += 1
                later = frozenset(nesting[name].get("then_without", ()))
        # A chain under an active reentry function stops where it meets that
        # function again, and one under a nesting function where that
        # function's count runs out, so its depth holds only for that state.
        barred = frozenset().union(*banned, later or ())
        key = (title, barred,
               frozenset(bare(item) for item in active if bare(item) in reentry),
               tuple(sorted((item, active.count(item)) for item in set(active)
                            if bare(item) in nesting)))
        if key in memo:
            return memo[key]
        if title in active and name not in nesting:
            # A function may recur once a reentry or nesting function has
            # been entered after it: the first makes no call on its second
            # entry and the second is entered a bounded number of times, so
            # the chain still ends.
            last = len(active) - 1 - active[::-1].index(title)
            if not any(bare(item) in reentry or bare(item) in nesting
                       for item in active[last + 1:]):
                cycle = active[last:] + [title]
                raise ModelError("cycle: " + " -> ".join(bare(item) for item in cycle))
        if name in recursion:
            frame *= recursion[name]["depth"]
        active.append(title)
        if later:
            banned.append(later)
        best = (0, [])
        for callee in sorted(callees):
            callee_name = bare(callee)
            if callee_name == name and name in recursion:
                continue
            if (name, callee_name) in cuts:
                cuts[(name, callee_name)] += 1
                continue
            if callee_name in excluded or callee_name in barred:
                continue
            guard = guards.get((name, callee_name))
            if guard:
                # The call's arguments keep the callee from the named
                # functions, here and in everything it calls.
                guard[1] += 1
                banned.append(guard[0])
            result = depth(node(callee, title))
            if guard:
                banned.pop()
            if result[0] > best[0]:
                best = result
        active.pop()
        if later:
            banned.pop()
        memo[key] = (frame + best[0], [(name, frame), *best[1]])
        return memo[key]

    results = []
    for root in model["roots"]:
        if root["entry"] is None:
            # A handler in assembly that leaves this stack before it calls
            # anything: the root costs what the entry itself pushes here.
            total, path = 0, []
        else:
            found = titles.get(root["entry"], [root["entry"]])
            total, path = depth(found[0])
        if root.get("pushed"):
            total, path = total + root["pushed"], [("entry", root["pushed"]), *path]
        stacks = root["stack"] if isinstance(root["stack"], list) else [root["stack"]]
        results.append({"name": root["name"], "entry": root["entry"],
                        "stacks": stacks, "role": root["role"],
                        "level": root.get("level"), "bytes": total, "path": path,
                        "requested": root.get("requested", False)})
    unused = [f"{caller} -> {callee}" for (caller, callee), count in cuts.items()
              if not count]
    if unused:
        raise ModelError("cut matches no edge reached: " + ", ".join(sorted(unused)))
    unused = [f"{caller} -> {callee}" for (caller, callee), guard in guards.items()
              if not guard[1]]
    if unused:
        raise ModelError("guard matches no edge reached: " + ", ".join(sorted(unused)))
    unused = sorted(name for name, count in reentry.items() if not count)
    if unused:
        raise ModelError("reentry function is never re-entered: " + ", ".join(unused))
    for name in recursion:
        if not any(bare(key[0]) == name for key in memo):
            raise ModelError(f"recursion entry {name} is not reached")
    for name, entry in nesting.items():
        if not again[name]:
            raise ModelError(f"nesting function {name} is never entered twice")
        if entry["cut"] and not stopped[name]:
            raise ModelError(f"nesting function {name} never reaches its bound")
        if not entry["cut"] and stopped[name]:
            raise ModelError(f"nesting function {name} is entered more than "
                             f"{entry['depth']} times")
    return results


def overlay_total(mine, base, levels, name):
    """One stack's total with an overlay counted."""

    def worst(field, role, level=None):
        return max((r["overlays"][name].get(field, r["overlays"][name]["bytes"])
                    for r in mine if r["role"] == role and r["level"] == level),
                   default=0)

    def above(skip, field):
        return sum(EXCEPTION_FRAME + worst(field, "interrupt", level)
                   for level in levels if level != skip)

    if not any("later" in r["overlays"][name] for r in mine):
        # Every chain can meet the overlay: all are counted with it.
        return worst("bytes", "base") + above(None, "bytes")
    # The overlay's callee is entered afresh once. Either the base chain
    # enters it and every handler above runs after that, or one handler
    # enters it over an untouched base chain while the other handlers may
    # run after it.
    return max([worst("bytes", "base") + above(None, "later")] +
               [base["bytes"] + EXCEPTION_FRAME + worst("bytes", "interrupt", level) +
                above(level, "later") for level in levels])


def totals(results, model):
    """Each stack's base chain plus one exception frame and handler per level."""
    summary = []
    for stack in model["stacks"]:
        mine = [r for r in results if stack["name"] in r["stacks"]]
        base = max((r for r in mine if r["role"] == "base"), key=lambda r: r["bytes"])
        levels = {}
        for result in mine:
            if result["role"] == "interrupt":
                best = levels.get(result["level"])
                if best is None or result["bytes"] > best["bytes"]:
                    levels[result["level"]] = result
        nested = sum(EXCEPTION_FRAME + r["bytes"] for r in levels.values())
        # An overlay's total takes every chain with the overlay counted,
        # since a base chain and a handler above it can each meet it.
        over = {name: overlay_total(mine, base, levels, name) for name in sorted(
            set().union(*(r.get("overlays", {}) for r in mine)))}
        summary.append({"overlays": over, "stack": stack["name"],
                        "budget": stack["bytes"],
                        "base": base["name"], "base_bytes": base["bytes"],
                        "levels": {level: {"root": r["name"],
                                           "bytes": EXCEPTION_FRAME + r["bytes"]}
                                   for level, r in sorted(levels.items())},
                        "total": base["bytes"] + nested,
                        "free": stack["bytes"] - base["bytes"] - nested})
    return summary


MODEL_LISTS = ("stacks", "roots", "sites", "cuts", "recursion", "nesting", "reentry",
               "guards", "exempt", "excluded")


def select(model, config):
    """The model's entries for one kernel configuration."""
    known = model.get("configs")
    if known is None:
        return model
    if config not in known:
        raise ModelError(f"the model does not name configuration {config}")
    chosen = dict(model)
    for key in MODEL_LISTS:
        kept = []
        for entry in model.get(key, ()):
            names = entry.get("configs")
            if names is not None:
                stray = sorted(set(names) - set(known))
                if stray or not names:
                    raise ModelError(f"{key} entry names configurations the model "
                                     f"does not: {', '.join(stray) or 'none'}")
                if config not in names:
                    continue
            kept.append(entry)
        chosen[key] = kept
    return chosen


def check_layout(model, image):
    """Hold the model's budgets and counts to the linked image's own."""
    for stack in model["stacks"]:
        if "object" in stack:
            if stack["object"] not in image.symbols:
                raise ModelError(f"stack {stack['name']}: the image has no object "
                                 f"{stack['object']}")
            if image.symbols[stack["object"]][1] != stack["bytes"]:
                raise ModelError(
                    f"stack {stack['name']} is {image.symbols[stack['object']][1]} "
                    f"bytes in the image, {stack['bytes']} in the model")
        if "top" not in stack:
            continue
        for name in (stack["top"], stack["base"]):
            if name not in image.addresses:
                raise ModelError(f"stack {stack['name']}: the image has no {name}")
        span = image.addresses[stack["top"]] - image.addresses[stack["base"]]
        span -= image.struct(stack["struct"])[0] if "struct" in stack else 0
        if span != stack["bytes"]:
            raise ModelError(f"stack {stack['name']} is {span} bytes in the image, "
                             f"{stack['bytes']} in the model")
    for entry in model.get("nesting", ()):
        if "elements" not in entry:
            continue
        if entry["elements"] not in image.symbols:
            raise ModelError(f"the image has no object {entry['elements']}")
        count, rest = divmod(image.symbols[entry["elements"]][1],
                             image.struct(entry["struct"])[0])
        if rest or count != entry["depth"]:
            raise ModelError(f"nesting function {entry['function']} is bounded at "
                             f"{entry['depth']}; {entry['elements']} holds {count}")


def overlay_model(model, name=None, later=False):
    """The model with one overlay's left-out calls counted, or with none.

    An overlay names calls the plain bound leaves out and what bounds them:
    an "excluded" callee that carries the overlay's name and an "on_top"
    nesting rule, a site rule's "left_out" targets under that name, and a
    "nesting" entry marked with it. "later" gives the chains of a root that
    runs after another chain has already entered the overlay's callee once.
    """
    known = {entry["name"] for entry in model.get("overlays", ())}
    if name is not None and name not in known:
        raise ModelError(f"the model has no overlay {name}")
    chosen = dict(model)
    chosen["excluded"], chosen["sites"] = [], []
    chosen["nesting"] = [entry for entry in model.get("nesting", ())
                         if entry.get("overlay", name) == name]
    used = {entry["overlay"] for entry in model.get("nesting", ())
            if "overlay" in entry}
    for entry in model.get("excluded", ()):
        used.add(entry.get("overlay"))
        if "overlay" in entry and entry["overlay"] == name:
            chosen["nesting"].append({"function": entry["callee"], **entry["on_top"],
                                      "entered": int(later)})
        else:
            chosen["excluded"].append(entry)
    for rule in model.get("sites", ()):
        barred = list(rule.get("except", ()))
        for overlay, targets in rule.get("left_out", {}).items():
            used.add(overlay)
            if overlay != name:
                barred += targets
        chosen["sites"].append({**rule, "except": barred} if barred else rule)
    stray = sorted(used - known - {None}) + sorted(known - used)
    if stray:
        raise ModelError("overlays named and overlays used differ: " + ", ".join(stray))
    return chosen


def run(model_path, build_dir, objdump, extra=(), tables=(), config=None):
    build = Path(build_dir)
    model = select(json.loads(Path(model_path).read_text(encoding="utf-8")),
                   config or build.resolve().name)
    texts = [path.read_text(encoding="utf-8") for path in sorted(build.glob("*.ci"))]
    if not texts:
        raise ModelError(f"{build} holds no call-graph files")
    frames, edges, sites, places = load_graph(texts)
    image = Image(build / "unix.elf", objdump)
    check_layout(model, image)
    extra = list(extra)
    for table in tables:
        extra += sorted(image.table_targets({"table": table}))
    stack = model["stacks"][0]["name"]
    model["roots"] = model["roots"] + [
        {"name": name, "entry": name, "stack": stack, "role": "separate",
         "requested": True}
        for name in extra]
    cache = {}

    def source_line(path, line):
        if path not in cache:
            cache[path] = (build / path).read_text(encoding="utf-8").splitlines()
        return cache[path][line - 1] if line <= len(cache[path]) else ""

    taken = {name: image.same_address(name)
             for name in address_taken(build, image.code, objdump)}
    def chains(name=None, later=False):
        return analyze(frames, edges, sites, overlay_model(model, name, later),
                       image.functions, image.listing, image.table_targets,
                       source_line, taken, places)

    results = chains()
    for overlay in model.get("overlays", ()):
        for result, other in zip(results, chains(overlay["name"])):
            result.setdefault("overlays", {})[overlay["name"]] = {
                "label": overlay["label"], "bytes": other["bytes"],
                "path": other["path"]}
        if overlay.get("once"):
            for result, other in zip(results, chains(overlay["name"], True)):
                result["overlays"][overlay["name"]]["later"] = other["bytes"]
    live_frames = {bare(title): size for title, size in frames.items()
                   if bare(title) in image.functions}
    return {"roots": results, "stacks": totals(results, model), "frames": live_frames}


def report(result):
    for root in result["roots"]:
        print(f"{root['name']} {root['bytes']}")
        print("    " + (" -> ".join(f"{name}[{size}]" for name, size in root["path"])
                        or "(no compiled entry on this stack)"))
        for over in root.get("overlays", {}).values():
            if over["bytes"] != root["bytes"]:
                print(f"{root['name']}, {over['label']} {over['bytes']}")
                print("    " + " -> ".join(f"{name}[{size}]"
                                           for name, size in over["path"]))
    for stack in result["stacks"]:
        parts = [f"{stack['base']} {stack['base_bytes']}"]
        parts += [f"{level} {entry['root']} {entry['bytes']}"
                  for level, entry in stack["levels"].items()]
        print(f"stack {stack['stack']}: {' + '.join(parts)} = {stack['total']} "
              f"of {stack['budget']}, {stack['free']} free")
        for name, total in stack["overlays"].items():
            print(f"stack {stack['stack']}, overlay {name}: {total} "
                  f"of {stack['budget']}")


def self_test():
    def ci(nodes, edge_list):
        text = ""
        for name, size, kind in nodes:
            text += (f'node: {{ title: "{name}" label: "{name}\\nf.c:1:1\\n'
                     f'{size} bytes ({kind})" }}\n')
        for source, target in edge_list:
            text += (f'edge: {{ sourcename: "{source}" targetname: "{target}" '
                     'label: "f.c:7:3" }\n')
        return text

    nodes = [("root", 16, "static"), ("mid", 24, "static"), ("leaf", 8, "static"),
             ("irq", 40, "static"), ("cb", 100, "static"), ("spare", 4, "static")]
    edge_list = [("root", "mid"), ("mid", "leaf"), ("mid", INDIRECT), ("leaf", "helper")]
    model = {
        "stacks": [{"name": "s", "bytes": 512}],
        "roots": [{"name": "base", "entry": "root", "stack": "s", "role": "base"},
                  {"name": "tick", "entry": "irq", "stack": "s",
                   "role": "interrupt", "level": "clock"}],
        "sites": [{"function": "mid", "text": "fn(", "targets": ["cb"]}],
    }
    listing = {"helper": {"frame": 12, "calls": set(), "flaw": None}}
    live = {"root", "mid", "leaf", "irq", "cb", "helper", "spare"}

    def attempt(node_list, edges_in, model_in, listing_in=listing, taken=()):
        frames, edges, sites, places = load_graph([ci(node_list, edges_in)])
        results = analyze(frames, edges, sites, model_in, live | {"rec", "a", "b"},
                          listing_in, lambda rule: set(),
                          lambda path, line: "x = fn(1);", taken, places)
        return totals(results, model_in)

    good = attempt(nodes, edge_list, model)[0]
    # root 16 + mid 24 + cb 100 = 140; tick 36 + 40 = 76.
    if (good["base_bytes"], good["total"], good["free"]) != (140, 216, 296):
        raise ModelError(f"self-test: known-good graph gave {good}")
    cases = {
        "cycle": (nodes, edge_list + [("leaf", "root")], model, listing),
        "unresolved indirect site": (nodes, edge_list, {**model, "sites": []}, listing),
        "dynamic frame": ([("root", 16, "dynamic"), *nodes[1:]], edge_list, model,
                          listing),
        "unknown callee": (nodes, edge_list, model, {}),
        "unused site rule": (nodes, edge_list, {**model, "sites": model["sites"] + [
            {"function": "leaf", "text": "fn(", "targets": ["cb"]}]}, listing),
        "unused cut": (nodes, edge_list, {**model, "cuts": [
            {"caller": "spare", "callee": "leaf", "reason": "none"}]}, listing),
        "register stack adjustment": (nodes, edge_list, model, {"helper": {
            "frame": 12, "calls": set(), "flaw": "add sp, r3"}}),
    }
    flat = attempt(nodes, edge_list, {**model, "sites": [
        {"function": "mid", "text": "fn(", "frames": ["cb", "leaf"]}]})[0]
    # root 16 + mid 24 + (cb 100 + leaf 8, helper not reached) = 148.
    if flat["base_bytes"] != 148:
        raise ModelError(f"self-test: frames rule gave {flat}")
    guarded = attempt(nodes, edge_list, {**model, "guards": [
        {"caller": "root", "callee": "mid", "without": ["cb"], "require": "fn\\(1\\)",
         "reason": "none"}]})[0]
    # root 16 + mid 24 + leaf 8 + helper 12 = 60 once cb is barred under mid.
    if guarded["base_bytes"] != 60:
        raise ModelError(f"self-test: guard gave {guarded}")
    loop_model = {"stacks": model["stacks"], "roots": [
        {"name": "base", "entry": "a", "stack": "s", "role": "base"}]}
    rec_nodes = [("a", 8, "static"), ("rec", 10, "static")]
    rec = attempt(rec_nodes, [("a", "rec"), ("rec", "rec")], {**loop_model, "recursion": [
        {"function": "rec", "depth": 3, "reason": "none"}]})[0]
    # a 8 + three rec frames of 10 = 38.
    if rec["base_bytes"] != 38:
        raise ModelError(f"self-test: recursion gave {rec}")
    ring_nodes = [("a", 8, "static"), ("b", 20, "static")]
    ring = attempt(ring_nodes, [("a", "b"), ("b", "a")], {**loop_model, "reentry": [
        {"function": "b", "reason": "none"}]})[0]
    # a 8 + b 20 + a 8 + b's frame alone on its second entry, 20 = 56.
    if ring["base_bytes"] != 56:
        raise ModelError(f"self-test: reentry gave {ring}")
    cases["unbounded recursion"] = (rec_nodes, [("a", "rec"), ("rec", "rec")],
                                    loop_model, listing)
    cases["reentry never re-entered"] = (nodes, edge_list, {**model, "reentry": [
        {"function": "leaf", "reason": "none"}]}, listing)
    cases["recursion never reached"] = (nodes, edge_list, {**model, "recursion": [
        {"function": "spare", "depth": 2, "reason": "none"}]}, listing)
    cases["unused guard"] = (nodes, edge_list, {**model, "guards": [
        {"caller": "spare", "callee": "leaf", "without": ["cb"], "reason": "none"}]},
        listing)
    cases["guard whose call text differs"] = (nodes, edge_list, {**model, "guards": [
        {"caller": "root", "callee": "mid", "without": ["cb"], "require": "fn\\(2\\)",
         "reason": "none"}]}, listing)
    cases["uncovered address-taken function"] = (nodes, edge_list, model, listing,
                                                 {"spare"})
    covered = attempt(nodes, edge_list, {**model, "exempt": [
        {"function": "spare", "reason": "none"}]}, listing, {"spare", "cb", "irq"})[0]
    if covered["total"] != 216:
        raise ModelError(f"self-test: exemption gave {covered}")
    nest_nodes = [("a", 8, "static"), ("rec", 10, "static"), ("b", 20, "static")]
    nest_edges = [("a", "rec"), ("rec", "b"), ("b", "rec")]

    def nest(**fields):
        return {**loop_model, "nesting": [{"function": "rec", "reason": "none",
                                           **fields}]}

    cut_nest = attempt(nest_nodes, nest_edges, nest(depth=3, cut=True))[0]
    # a 8 + three rounds of rec 10 and b 20 = 98; the fourth rec is barred.
    if cut_nest["base_bytes"] != 98:
        raise ModelError(f"self-test: nesting gave {cut_nest}")
    once = attempt(nest_nodes, nest_edges,
                   nest(depth=2, cut=False, then_without=["b"]))[0]
    # a 8 + rec 10 + b 20 + rec 10, whose second entry may not call b = 48.
    if once["base_bytes"] != 48:
        raise ModelError(f"self-test: nesting with a barred callee gave {once}")
    cases["nesting entered past its bound"] = (nest_nodes, nest_edges,
                                               nest(depth=2, cut=False), listing)
    cases["nesting never entered twice"] = (nodes, edge_list, {**model, "nesting": [
        {"function": "leaf", "depth": 2, "cut": True, "reason": "none"}]}, listing)
    cases["nesting bound never reached"] = (
        nest_nodes, nest_edges, nest(depth=2, cut=True, then_without=["b"]), listing)
    left = {**loop_model, "overlays": [{"name": "fail", "label": "with a failure"}],
            "excluded": [{"callee": "rec", "reason": "none", "overlay": "fail",
                          "on_top": {"depth": 2, "cut": False, "then_without": ["b"],
                                     "reason": "none"}}]}
    stages = [attempt(nest_nodes, nest_edges, overlay_model(left, *choice))[0]["base_bytes"]
              for choice in ((), ("fail",), ("fail", True))]
    # Left out: a 8. Counted afresh: a 8 + rec 10 + b 20 + rec 10 = 48.
    # Counted as already entered once, rec may not call b: a 8 + rec 10 = 18.
    if stages != [8, 48, 18]:
        raise ModelError(f"self-test: a later entry gave {stages}")
    priced = attempt(nodes, edge_list, {**model, "sites": [
        {"function": "mid", "text": "fn(", "bytes": 50}]})[0]
    # root 16 + mid 24 + 50 for the callee outside the image = 90.
    if priced["base_bytes"] != 90:
        raise ModelError(f"self-test: bytes rule gave {priced}")
    top_model = {**model, "overlays": [{"name": "fail", "label": "with a failure"}],
                 "excluded": [{"callee": "leaf", "reason": "none", "overlay": "fail",
                               "on_top": {"depth": 2, "cut": False,
                                          "then_without": ["helper"],
                                          "reason": "none"}}]}
    plain = attempt(nodes, [*edge_list, ("cb", "leaf"), ("helper", "leaf")],
                    overlay_model(top_model),
                    {"helper": {"frame": 12, "calls": {"leaf"}, "flaw": None}})[0]
    lifted = attempt(nodes, [*edge_list, ("cb", "leaf"), ("helper", "leaf")],
                     overlay_model(top_model, "fail"),
                     {"helper": {"frame": 12, "calls": {"leaf"}, "flaw": None}})[0]
    # Left out, leaf ends no chain: root 16 + mid 24 + cb 100 = 140. Counted,
    # cb reaches leaf 8, helper 12 and leaf 8 again, whose second entry may
    # not call helper: 140 + 28 = 168.
    if (plain["base_bytes"], lifted["base_bytes"]) != (140, 168):
        raise ModelError(f"self-test: on_top gave {plain} and {lifted}")
    scoped = {"configs": ["A", "B"], **model, "sites": model["sites"] + [
        {"function": "leaf", "text": "fn(", "targets": ["cb"], "configs": ["B"]}]}
    if attempt(nodes, edge_list, select(scoped, "A"))[0]["total"] != 216:
        raise ModelError("self-test: an entry of another configuration was kept")
    cases["unused site rule of this configuration"] = (
        nodes, edge_list, select(scoped, "B"), listing)

    two = attempt(nodes, edge_list, {**model, "stacks": [
        {"name": "s", "bytes": 512}, {"name": "t", "bytes": 256}], "roots": [
        model["roots"][0],
        {**model["roots"][1], "stack": ["s", "t"]},
        {"name": "side", "entry": "leaf", "pushed": 8, "stack": "t", "role": "base"},
        {"name": "door", "entry": None, "stack": "s", "role": "interrupt",
         "level": "side"}]})
    # s: 140 + tick 76 + the door's exception frame 36 = 252. t: 8 pushed at
    # entry + leaf 8 + helper 12 = 28, + tick 76 = 104.
    if [entry["total"] for entry in two] != [252, 104]:
        raise ModelError(f"self-test: two stacks gave {two}")

    def chain(name, role, level, size, first, later=None):
        over = {"label": "x", "bytes": first}
        if later is not None:
            over["later"] = later
        return {"name": name, "stacks": ["s"], "role": role, "level": level,
                "bytes": size, "overlays": {"p": over}}

    stack_model = {"stacks": [{"name": "s", "bytes": 512}]}
    summed = totals([chain("b", "base", None, 100, 300), chain("i", "interrupt", "a", 20, 220),
                     chain("j", "interrupt", "b", 10, 50)], stack_model)[0]
    once_only = totals([chain("b", "base", None, 100, 300, 150),
                        chain("i", "interrupt", "a", 20, 220, 60),
                        chain("j", "interrupt", "b", 10, 50, 30)], stack_model)[0]
    # Every chain with the overlay: 300 + (36 + 220) + (36 + 50) = 642. One
    # fresh entry: the base's, 300 + (36 + 60) + (36 + 30) = 462, is deeper
    # than a's, 100 + (36 + 220) + (36 + 30) = 422.
    if (summed["overlays"]["p"], once_only["overlays"]["p"]) != (642, 462):
        raise ModelError(f"self-test: overlay totals gave {summed} and {once_only}")

    class Layout:
        addresses = {"top": 3072, "base": 0}
        symbols = {"pool": (0, 144)}

        @staticmethod
        def struct(name):
            return {"user": (1024, {}), "buf": (36, {})}[name]

    sized = {"stacks": [{"name": "s", "bytes": 2048, "top": "top", "base": "base",
                         "struct": "user"}],
             "nesting": [{"function": "rec", "depth": 4, "elements": "pool",
                          "struct": "buf"}]}
    check_layout(sized, Layout)
    kept_out = {**model, "overlays": [{"name": "wide", "label": "with cb"}],
                "sites": [{**model["sites"][0], "left_out": {"wide": ["cb"]}}]}
    # Without cb the site has no target left: root 16 + mid 24 + leaf 8 +
    # helper 12 = 60; the overlay puts cb back, 140.
    narrow = attempt(nodes, edge_list, {**overlay_model(kept_out), "sites": [
        {**overlay_model(kept_out)["sites"][0], "table": "t"}]})[0]
    wide = attempt(nodes, edge_list, overlay_model(kept_out, "wide"))[0]
    if (narrow["base_bytes"], wide["base_bytes"]) != (60, 140):
        raise ModelError(f"self-test: left_out gave {narrow} and {wide}")
    refusals = {
        "overlay the model does not name": lambda: overlay_model(top_model, "other"),
        "overlay no entry uses": lambda: overlay_model(
            {**model, "overlays": [{"name": "idle", "label": "none"}]}),
        "unknown configuration": lambda: select(scoped, "C"),
        "entry naming an unknown configuration": lambda: select(
            {**scoped, "exempt": [{"function": "spare", "configs": ["C"]}]}, "A"),
        "stack budget the image contradicts": lambda: check_layout(
            {"stacks": [{**sized["stacks"][0], "bytes": 4096}]}, Layout),
        "stack object the image contradicts": lambda: check_layout(
            {"stacks": [{"name": "r", "bytes": 100, "object": "pool"}]}, Layout),
        "nesting bound the image contradicts": lambda: check_layout(
            {"stacks": [], "nesting": [{**sized["nesting"][0], "depth": 8}]}, Layout),
    }
    for label, call in refusals.items():
        try:
            call()
        except ModelError as error:
            print(f"PASS refused {label}: {error}")
        else:
            raise ModelError(f"self-test: {label} was accepted")
    for label, arguments in cases.items():
        try:
            attempt(*arguments)
        except ModelError as error:
            print(f"PASS refused {label}: {error}")
        else:
            raise ModelError(f"self-test: {label} was accepted")
    cut = attempt(nodes, edge_list + [("leaf", "root")], {**model, "cuts": [
        {"caller": "leaf", "callee": "root", "reason": "guarded"}]})[0]
    if cut["total"] != 216:
        raise ModelError(f"self-test: cut graph gave {cut}")
    sample = ("00000010 <f>:\n  10:\tb5f0      \tpush\t{r4, r5, r6, r7, lr}\n"
              "  12:\tb083      \tsub\tsp, #12\n  14:\tf000 f800 \tbl\t40 <g>\n"
              "00000040 <g>:\n  40:\t449d      \tadd\tsp, r3\n")
    parsed = disassembly_frames(sample)
    returns = disassembly_frames(
        "00000010 <h>:\n  10:\tbc0e      \tpop\t{r1, r2, r3}\n"
        "  12:\t4342      \tmuls\tr2, r0\n"
        "  14:\t4718      \tbx\tr3\n00000020 <i>:\n  20:\tbc08      \tpop\t{r3}\n"
        "  22:\t4613      \tmov\tr3, r2\n  24:\t4718      \tbx\tr3\n")
    if returns["h"]["flaw"] or not returns["i"]["flaw"]:
        raise ModelError(f"self-test: return parse gave {returns}")
    if parsed["f"]["frame"] != 32 or parsed["f"]["calls"] != {"g"} or not parsed["g"]["flaw"]:
        raise ModelError(f"self-test: disassembly parse gave {parsed}")
    print("PASS known-good graph: 140 base, 216 total, 296 free; guard, frames, "
          "cut and disassembly cases agree")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--from", dest="extra", action="append", default=[],
                        metavar="FUNCTION",
                        help="also report the deepest chain from this function")
    parser.add_argument("--from-table", dest="tables", action="append", default=[],
                        metavar="TABLE",
                        help="also report each function a pointer table names")
    parser.add_argument("--config", metavar="NAME",
                        help="the kernel configuration (default: BUILD_DIR's name)")
    parser.add_argument("model", nargs="?")
    parser.add_argument("build_dir", nargs="?")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if not args.model or not args.build_dir:
            parser.error("MODEL and BUILD_DIR are required")
        result = run(args.model, args.build_dir, args.objdump, args.extra, args.tables,
                     args.config)
        report(result)
        if args.json:
            args.json.write_text(json.dumps(result, indent=1, sort_keys=True) + "\n",
                                 encoding="utf-8")
        return 0
    except (ModelError, OSError, subprocess.CalledProcessError) as error:
        print(f"stack_chain: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
