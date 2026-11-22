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
import hashlib
import io
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


def adapt(original, slots, state_names=()):
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
    common = 0
    end = symtab
    while end < len(original) and original[end]:
        length = original[end]
        require(end + 6 + length <= len(original), "symbol extent differs")
        names.append(original[end + 6 : end + 6 + length])
        types.append(original[end + 1])
        if types[-1] == 0x26:
            common += (struct.unpack_from("<I", original, end + 2)[0] + 3) & ~3
        if names[-1] in state_names and types[-1] & 0x20:
            require(types[-1] == 0x20, "application defines a shared state symbol")
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
    return result, calls.keys(), data + bss + common


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


STATE = "__xip_libc_state"


def object_layout(image, record):
    from elftools.elf.elffile import ELFFile

    source = image.read_bytes()
    require(
        hashlib.sha256(source).hexdigest() == record["slot_elf_sha256"],
        "state image digest differs",
    )
    elf = ELFFile(io.BytesIO(source))
    table = elf.get_section_by_name(".symtab")
    extent = table.get_symbol_by_name("__libc_state_size")
    require(
        extent and len(extent) == 1 and extent[0]["st_value"] == record["state"],
        "state image size differs",
    )
    layout = {}
    for name in [*record["objects"], "environ", "__progname"]:
        require(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name), "invalid object name")
        symbols = table.get_symbol_by_name(name)
        require(symbols and len(symbols) == 1, "object owner differs")
        symbol = symbols[0]
        section = elf.get_section(symbol["st_shndx"])
        addr, size = symbol["st_value"], symbol["st_size"]
        require(
            symbol["st_info"]["type"] == "STT_OBJECT"
            and size > 0
            and section["sh_flags"] & 2
            and section["sh_addr"] <= addr <= section["sh_addr"] + section["sh_size"] - size,
            "object extent differs",
        )
        layout[name] = (addr, size, bool(section["sh_flags"] & 1))
    return layout


def reservation(layout, size, padding):
    lines = [".syntax unified", ".thumb", ".bss"]
    if padding:
        lines.append(f".space {padding}")
    lines.extend([f".global {STATE}", f"{STATE}:"])
    cursor = 0
    for name, (address, width, writable) in sorted(layout.items(), key=lambda item: item[1][0]):
        lines.append(f".global {name}")
        if writable:
            require(cursor <= address <= size - width, "overlapping or out-of-state object")
            if address > cursor:
                lines.append(f".space {address - cursor}")
            lines.extend([f"{name}:", f".space {width}"])
            cursor = address + width
        else:
            lines.append(f".equ {name},{address}")
    if cursor < size:
        lines.append(f".space {size - cursor}")
    return "\n".join(lines) + "\n"


def link(inputs, state_image=None):
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
    layout = object_layout(state_image, record) if state_image is not None else {}
    state_names = {n.encode() for n in [*layout, STATE]} if layout else set()
    obj, calls, writable_bytes = adapt(
        (work / "prelink.o").read_bytes(),
        {n.encode(): 0x10020001 + 8 * i for i, group in enumerate(groups) for n in group},
        state_names,
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
    if layout:
        pad = -writable_bytes % 8
        (work / "state.s").write_text(reservation(layout, record["state"], pad))
        subprocess.run([TOOLS / "as", "state.s", "-o", "state.o"], cwd=work, check=True)
        objects.append("state.o")
    subprocess.run([TOOLS / "ld", "-o", "app.aout", *objects], cwd=work, check=True)
    if layout:
        image = work / "app.aout"
        _, text, data, bss, *_ = HEADER.unpack_from(image.read_bytes())
        base = 0x20000000 + text + data + bss - record["state"]
        require(base % 8 == 0 and bss >= record["state"], "final state reservation differs")
        output = subprocess.check_output([TOOLS / "nm", image], text=True)
        symbols = {
            n: (int(a, 16), kind)
            for a, kind, n in (
                line.split() for line in output.splitlines() if len(line.split()) == 3
            )
            if kind in ("B", "A")
        }
        require(symbols.get(STATE) == (base, "B"), "state does not end at the heap boundary")
        for name, (address, _, writable) in layout.items():
            expected = (base + address, "B") if writable else (address, "A")
            require(symbols.get(name) == expected, "final object binding differs: " + name)
    return work / "app.aout"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "inputs", type=Path, nargs="+", help="application objects and archives in link order"
    )
    parser.add_argument(
        "--state-image", type=Path, help="reserve objects from the qualified compact diagnostic ELF"
    )
    args = parser.parse_args()
    print(link([p.resolve(strict=True) for p in args.inputs], args.state_image))
