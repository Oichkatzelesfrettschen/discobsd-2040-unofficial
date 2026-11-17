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
    bounded recursions, the functions that stop on a second entry, the
    calls whose arguments keep the callee from named functions, the edges
    cut as unreachable and the callees left out by name. The program
    prints, for each root, the bytes of the deepest chain and its path,
    then each stack's total against its budget.

stack_chain.py --self-test
    Run the analysis over synthetic graphs: known-good graphs must give
    their hand-computed depths, and a cycle, an unresolved indirect site, a
    dynamic frame, an unknown callee, an address-taken function no entry
    names and each kind of unused model entry must be refused.

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
        self.code = set()
        for symbol in self.elf.get_section_by_name(".symtab").iter_symbols():
            kind = symbol["st_info"]["type"]
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
    rules = model.get("sites", ())
    used = [0] * len(rules)
    calls, flat = {}, {}
    covered = {root["entry"] for root in model["roots"]}
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
        # A chain under an active reentry function stops where it meets that
        # function again, so its depth holds only for that set of functions.
        barred = frozenset().union(*banned)
        key = (title, barred, frozenset(bare(item) for item in active
                                        if bare(item) in reentry))
        if key in memo:
            return memo[key]
        if title in active:
            # A function may recur once a reentry function has been entered
            # after it: that function's second entry makes no call, so the
            # chain still ends.
            last = len(active) - 1 - active[::-1].index(title)
            if not any(bare(item) in reentry for item in active[last + 1:]):
                cycle = active[last:] + [title]
                raise ModelError("cycle: " + " -> ".join(bare(item) for item in cycle))
        if name in recursion:
            frame *= recursion[name]["depth"]
        active.append(title)
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
        memo[key] = (frame + best[0], [(name, frame), *best[1]])
        return memo[key]

    results = []
    for root in model["roots"]:
        found = titles.get(root["entry"], [root["entry"]])
        total, path = depth(found[0])
        results.append({"name": root["name"], "entry": root["entry"],
                        "stack": root["stack"], "role": root["role"],
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
    return results


def totals(results, model):
    """Each stack's base chain plus one exception frame and handler per level."""
    summary = []
    for stack in model["stacks"]:
        mine = [r for r in results if r["stack"] == stack["name"]]
        base = max((r for r in mine if r["role"] == "base"), key=lambda r: r["bytes"])
        levels = {}
        for result in mine:
            if result["role"] == "interrupt":
                best = levels.get(result["level"])
                if best is None or result["bytes"] > best["bytes"]:
                    levels[result["level"]] = result
        nested = sum(EXCEPTION_FRAME + r["bytes"] for r in levels.values())
        summary.append({"stack": stack["name"], "budget": stack["bytes"],
                        "base": base["name"], "base_bytes": base["bytes"],
                        "levels": {level: {"root": r["name"],
                                           "bytes": EXCEPTION_FRAME + r["bytes"]}
                                   for level, r in sorted(levels.items())},
                        "total": base["bytes"] + nested,
                        "free": stack["bytes"] - base["bytes"] - nested})
    return summary


def run(model_path, build_dir, objdump, extra=(), tables=()):
    model = json.loads(Path(model_path).read_text(encoding="utf-8"))
    build = Path(build_dir)
    texts = [path.read_text(encoding="utf-8") for path in sorted(build.glob("*.ci"))]
    if not texts:
        raise ModelError(f"{build} holds no call-graph files")
    frames, edges, sites, places = load_graph(texts)
    image = Image(build / "unix.elf", objdump)
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

    results = analyze(frames, edges, sites, model, image.functions, image.listing,
                      image.table_targets, source_line,
                      {name: image.same_address(name)
                       for name in address_taken(build, image.code, objdump)},
                      places)
    live_frames = {bare(title): size for title, size in frames.items()
                   if bare(title) in image.functions}
    return {"roots": results, "stacks": totals(results, model), "frames": live_frames}


def report(result):
    for root in result["roots"]:
        print(f"{root['name']} {root['bytes']}")
        print("    " + " -> ".join(f"{name}[{size}]" for name, size in root["path"]))
    for stack in result["stacks"]:
        parts = [f"{stack['base']} {stack['base_bytes']}"]
        parts += [f"{level} {entry['root']} {entry['bytes']}"
                  for level, entry in stack["levels"].items()]
        print(f"stack {stack['stack']}: {' + '.join(parts)} = {stack['total']} "
              f"of {stack['budget']}, {stack['free']} free")


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
    parser.add_argument("model", nargs="?")
    parser.add_argument("build_dir", nargs="?")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if not args.model or not args.build_dir:
            parser.error("MODEL and BUILD_DIR are required")
        result = run(args.model, args.build_dir, args.objdump, args.extra, args.tables)
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
