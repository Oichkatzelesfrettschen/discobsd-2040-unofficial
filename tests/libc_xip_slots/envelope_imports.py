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

"""Emit Thumb import symbols for the measured shipped-member diagnostic image."""

import argparse
import hashlib
import io
import json
import re
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from symbols import require

RECORD = (
    Path(__file__).resolve().parents[2]
    / "docs/research/xip-shared-libc/shipped-member-envelope.json"
)
BASE = 0x10020000


def image_record(image):
    record = json.loads(RECORD.read_text())
    provider = json.loads(RECORD.with_name("compact-provider.json").read_text())
    if hashlib.sha256(image).hexdigest() == provider["slot_elf_sha256"]:
        record.update(provider)
    return record


def import_source(image, record):
    require(hashlib.sha256(image).hexdigest() == record["slot_elf_sha256"], "image digest differs")
    elf = ELFFile(io.BytesIO(image))
    text = elf.get_section_by_name(".text")
    table = elf.get_section_by_name(".symtab")
    groups = record["slot_groups"]
    width = record["slot_width_bytes"]
    require(width == 8 and text["sh_addr"] == BASE, "compact slot layout differs")
    names = [name for group in groups for name in group]
    require(len(names) == len(set(names)), "duplicate import")
    require(set(names) == set(record["roots"]) - set(record["objects"]), "callable set differs")
    data = text.data()
    lines = [".syntax unified", ".thumb", ".text"]
    for index, group in enumerate(groups):
        offset = record.get("slot_table_offset", 0) + width * index
        require(group and offset + width <= len(data), "slot extent differs")
        direct = group == [record.get("inline_trampoline")]
        slot = table.get_symbol_by_name("sigtramp" if direct else f"slot_{index}")
        require(
            slot and len(slot) == 1 and slot[0]["st_value"] == BASE + offset + 1,
            "slot address differs",
        )
        target = BASE + struct.unpack_from("<H", data, offset + 6)[0]
        for name in group:
            require(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name), "invalid import name")
            owner_name = record.get("implementation_owners", {}).get(name, name)
            owner = table.get_symbol_by_name(owner_name)
            if direct:
                require(
                    data[offset : offset + 4] == bytes.fromhex("67df01df")
                    and slot[0]["st_size"] == 4,
                    "signal slot instructions differ",
                )
                target = BASE + offset + 1
            elif owner_name != name:
                public = table.get_symbol_by_name(name)
                require(
                    public and len(public) == 1 and public[0]["st_value"] == BASE + offset + 1,
                    "public callback slot differs: " + name,
                )
            require(
                owner and len(owner) == 1 and owner[0]["st_value"] == target and target & 1,
                "slot target differs: " + name,
            )
            lines.extend(
                [
                    f".global {name}",
                    f".type {name}, %function",
                    ".thumb_func",
                    f"{name}:",
                    f".size {name}, {width}",
                ]
            )
        lines.append(f".space {width}")
    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    source = parser.parse_args().image.read_bytes()
    print(import_source(source, image_record(source)))
