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

"""Link a native diagnostic SRAM caller against the measured compact XIP slots."""

import argparse
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/bin/rp2040"
RECORD = ROOT / "docs/research/xip-shared-libc/shipped-member-envelope.json"
HEADER = struct.Struct("<8I")
BRIDGE = b"__xip_call_"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def adapt(original, slots):
    """Redirect external Thumb calls and preserve public absolute pointers."""
    require(len(original) >= HEADER.size, "short native header")
    magic, text, data, bss, rt, rd, symbols, entry = HEADER.unpack_from(original)
    require(magic == 0x008F0106, "expected a native Thumb relocatable")
    require(entry < text, "expected zero-origin prelink")
    text_reloc = HEADER.size + text + data
    symtab = text_reloc + rt + rd
    require(symtab + symbols == len(original), "object extent differs")
    names = []
    types = []
    records = []
    end = symtab
    while end < len(original) and original[end]:
        length = original[end]
        require(end + 6 + length <= len(original), "symbol extent differs")
        names.append(original[end + 6 : end + 6 + length])
        types.append(original[end + 1])
        records.append(end)
        end += 6 + length
    require(not any(original[end:]), "nonzero symbol padding")
    known_names = set(names)
    result = bytearray(original[:end])
    calls = {}
    for start, extent, segment, size in (
        (text_reloc, rt, HEADER.size, text),
        (text_reloc + rt, rd, HEADER.size + text, data),
    ):
        pos = start
        limit = start + extent
        while pos < limit and original[pos]:
            flags = original[pos]
            external = flags & 0x70 == 0x70
            width = 8 if external else 5
            require(pos + width <= limit, "short relocation")
            if external:
                index = int.from_bytes(original[pos + 5 : pos + 8], "little")
                require(index < len(names), "invalid relocation symbol")
                name = names[index]
                if name in slots and types[index] == 0x20:
                    offset = struct.unpack_from("<I", original, pos + 1)[0]
                    require(
                        flags in (0x71, 0x78) and offset + 4 <= size, "invalid import relocation"
                    )
                    # An absolute definition keeps the pointer binding outside ld's
                    # per-object table of unresolved relocation symbols.
                    struct.pack_into("<BI", result, records[index] + 1, 0x21, slots[name])
                    if flags == 0x78:
                        require(segment == HEADER.size and offset % 2 == 0, "invalid call location")
                        require(
                            original[segment + offset : segment + offset + 4]
                            == bytes.fromhex("00f000f8"),
                            "expected Thumb BL with zero addend",
                        )
                        require(BRIDGE + name not in known_names, "reserved bridge collision")
                        ordinal = calls.setdefault(name, len(names) + len(calls))
                        result[pos + 5 : pos + 8] = ordinal.to_bytes(3, "little")
            pos += width
        require(not any(original[pos:limit]), "nonzero relocation padding")
    for name in calls:
        bridge = BRIDGE + name
        result.extend(struct.pack("<BBI", len(bridge), 0x20, 0) + bridge)
    result.extend(bytes(-len(result) % 4))
    struct.pack_into("<I", result, 24, len(result) - symtab)
    return result, calls.keys()


def assembly(groups, calls, first):
    lines = [".syntax unified", ".thumb", ".text"]
    for ordinal, group in enumerate(groups, first):
        called = [name for name in group if name.encode() in calls]
        if called:
            lines.append(".p2align 2")
            for name in called:
                bridge = BRIDGE.decode() + name
                lines.extend([f".global {bridge}", ".thumb_func", f"{bridge}:"])
            lines.extend(
                [
                    "push {r3,r4}",
                    f"ldr r3,={0x10020001 + 8 * ordinal}",
                    "str r3,[sp,#4]",
                    "pop {r3,pc}",
                    ".pool",
                ]
            )
    return "\n".join(lines) + "\n"


def link(inputs):
    record = json.loads(RECORD.read_text())
    groups = record["slot_groups"]
    names = [name for group in groups for name in group]
    require(record["slot_width_bytes"] == 8 and all(groups), "invalid compact slot layout")
    require(
        len(names) == len(set(names))
        and all(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", n) for n in names),
        "invalid import names",
    )
    require(set(names) == set(record["roots"]) - set(record["objects"]), "callable set differs")
    work = Path(tempfile.mkdtemp(prefix="native-slot-imports-", dir=ROOT / ".build-logs"))
    subprocess.run([TOOLS / "ld", "-T0", "-r", "-o", "prelink.o", *inputs], cwd=work, check=True)
    obj, calls = adapt(
        (work / "prelink.o").read_bytes(),
        {n.encode(): 0x10020001 + 8 * i for i, group in enumerate(groups) for n in group},
    )
    (work / "caller.o").write_bytes(obj)
    objects = ["caller.o"]
    for first in range(0, len(groups), 64):
        batch = groups[first : first + 64]
        if any(name.encode() in calls for group in batch for name in group):
            stem = f"imports-{first}"
            (work / (stem + ".s")).write_text(assembly(batch, calls, first))
            subprocess.run([TOOLS / "as", stem + ".s", "-o", stem + ".o"], cwd=work, check=True)
            objects.append(stem + ".o")
    subprocess.run([TOOLS / "ld", "-o", "app.aout", *objects], cwd=work, check=True)
    return work / "app.aout"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "inputs", type=Path, nargs="+", help="application objects and archives in link order"
    )
    print(link([p.resolve(strict=True) for p in parser.parse_args().inputs]))
