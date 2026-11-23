"""Check ELF function owners for the shared callable-identity fixture."""

import io
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def require(condition, message):
    if not condition:
        raise ValueError(message)


def functions(elf, names, size):
    table = elf.get_section_by_name(".symtab")
    require(table is not None, "symbol table is absent")
    result = {}
    for name in names:
        found = table.get_symbol_by_name(name)
        require(found is not None and len(found) == 1, "function owner differs: " + name)
        symbol = found[0]
        require(symbol["st_info"]["type"] == "STT_FUNC", "function type differs: " + name)
        require(symbol["st_value"] & 1, "Thumb entry differs: " + name)
        require(symbol["st_size"] == size, "function extent differs: " + name)
        result[name] = symbol
    return result


def separate(source, destination, groups):
    """Replace reviewed public definitions with imports of their callable slots."""
    original = Path(source).read_bytes()
    data = bytearray(original)
    elf = ELFFile(io.BytesIO(original))
    require(
        elf.elfclass == 32
        and elf.little_endian
        and elf["e_machine"] == "EM_ARM"
        and elf["e_type"] == "ET_REL",
        "separation requires little-endian ARM ELF32 relocatable input",
    )
    require(
        not any(section.name.startswith(".gnu.lto_") for section in elf.iter_sections()),
        "LTO input requires native lowering before separation",
    )
    require(groups and all(groups), "alias groups differ")
    names = tuple(name for group in groups for name in group)
    require(names and len(names) == len(set(names)), "public names differ")
    table = elf.get_section_by_name(".symtab")
    require(table is not None and table["sh_entsize"] == 16, "symbol table layout differs")
    symbols = {}
    requested = set(names) | {"__libc_impl_" + name for name in names}
    for index, symbol in enumerate(table.iter_symbols()):
        if symbol.name in requested:
            require(symbol.name not in symbols, "duplicate symbol name: " + symbol.name)
            symbols[symbol.name] = (index, symbol)
    edits = []
    owners = {}
    for name in names:
        private_name = "__libc_impl_" + name
        require(
            name in symbols and private_name in symbols, "implementation alias is absent: " + name
        )
        index, public = symbols[name]
        private_index, private = symbols[private_name]
        section_index = public["st_shndx"]
        require(
            isinstance(section_index, int) and section_index > 0,
            "public definition is absent: " + name,
        )
        section = elf.get_section(section_index)
        require(section["sh_flags"] & 6 == 6, "provider lacks executable storage: " + name)
        require(
            public["st_info"]["type"] == private["st_info"]["type"] == "STT_FUNC"
            and public["st_info"]["bind"] in ("STB_GLOBAL", "STB_WEAK")
            and private["st_info"]["bind"] == "STB_GLOBAL"
            and public["st_other"]["visibility"] == "STV_DEFAULT",
            "provider type, binding or visibility differs: " + name,
        )
        require(
            public["st_value"] & 1
            and public["st_size"] > 0
            and (public["st_value"] & ~1) + public["st_size"] <= section["sh_size"],
            "provider Thumb extent differs: " + name,
        )
        require(
            all(public[field] == private[field] for field in ("st_shndx", "st_value", "st_size")),
            "implementation alias owner differs: " + name,
        )
        owners[name] = tuple(public[field] for field in ("st_shndx", "st_value", "st_size"))
        offset = table["sh_offset"] + index * 16
        fields = list(struct.unpack_from("<IIIBBH", data, offset))
        fields[1] = fields[2] = fields[5] = 0
        struct.pack_into("<IIIBBH", data, offset, *fields)
        private_offset = table["sh_offset"] + private_index * 16 + 13
        data[private_offset] = (data[private_offset] & ~3) | 2
        edits.append(
            {
                "public": name,
                "implementation": private_name,
                "provider_offset": public["st_value"],
                "provider_size": public["st_size"],
            }
        )
    for group in groups:
        for name in group[1:]:
            require(owners[name] == owners[group[0]], "alias group implementation differs: " + name)
    rewritten = ELFFile(io.BytesIO(data))
    for before, after in zip(elf.iter_sections(), rewritten.iter_sections()):
        if before.name != ".symtab":
            require(before.data() == after.data(), "separation changes a section: " + before.name)
    with Path(destination).open("xb") as stream:
        stream.write(data)
    return edits


def state_relocations(elf, got, initialized):
    cells = set()
    got_owners: dict[int, bool] = {}
    for section in elf.iter_sections():
        if not isinstance(section, RelocationSection):
            continue
        target = elf.get_section(section["sh_info"])
        reloc_symbols = elf.get_section(section["sh_link"])
        for relocation in section.iter_relocations():
            symbol = reloc_symbols.get_symbol(relocation["r_info_sym"])
            owner_index = symbol["st_shndx"]
            state = isinstance(owner_index, int) and bool(
                elf.get_section(owner_index)["sh_flags"] & 1
            )
            kind = relocation["r_info_type"]
            offset = relocation["r_offset"]
            if kind == 26 and target["sh_flags"] & 4:
                displacement = struct.unpack_from("<I", target.data(), offset - target["sh_addr"])[
                    0
                ]
                require(
                    displacement % 4 == 0 and displacement + 4 <= got["sh_size"],
                    "GOT displacement differs",
                )
                require(
                    struct.unpack_from("<I", got.data(), displacement)[0] == symbol["st_value"],
                    "GOT symbol value differs",
                )
                require(
                    got_owners.setdefault(displacement, state) == state,
                    "GOT ownership conflicts",
                )
            if kind == 2 and target["sh_flags"] & 2 and state:
                require(target["sh_flags"] & 1, "read-only absolute state reference")
                cells.add(offset)
    require(set(got_owners) == set(range(0, got["sh_size"], 4)), "GOT ownership is incomplete")
    cells.update(
        got["sh_addr"] + displacement for displacement, state in got_owners.items() if state
    )
    require(
        all(offset % 4 == 0 and offset + 4 <= initialized for offset in cells),
        "state relocation containment differs",
    )
    return sorted(cells)


def layout(path, groups, width=12):
    with Path(path).open("rb") as stream:
        elf = ELFFile(stream)
        table = elf.get_section_by_name(".symtab")
        require(table is not None, "linked symbol table is absent")
        symbols = {symbol.name: symbol for symbol in table.iter_symbols() if symbol.name}
        for index, group in enumerate(groups):
            entries = functions(elf, group, width)
            implementation_owner = None
            for name, entry in entries.items():
                require(
                    entry["st_value"] == 0x10020001 + index * width,
                    "canonical alias address differs: " + name,
                )
                require(
                    entry["st_info"]["bind"] == "STB_GLOBAL"
                    and entry["st_other"]["visibility"] == "STV_DEFAULT",
                    "public slot visibility or binding differs: " + name,
                )
                private = symbols["__libc_impl_" + name]
                require(
                    private["st_other"]["visibility"] == "STV_HIDDEN"
                    and private["st_info"]["type"] == "STT_FUNC"
                    and private["st_value"] != entry["st_value"],
                    "implementation ownership differs: " + name,
                )
                owner = tuple(private[field] for field in ("st_shndx", "st_value", "st_size"))
                if implementation_owner is None:
                    implementation_owner = owner
                require(
                    owner == implementation_owner, "alias group implementation differs: " + name
                )
        extent = symbols["__state_end"]["st_value"]
        initialized = symbols["__init_end"]["st_value"]
        flash_end = symbols["__flash_end"]["st_value"]
        got = elf.get_section_by_name(".got")
        require(got is not None and got["sh_size"] % 4 == 0, "GOT layout differs")
        require(
            0 < initialized <= extent <= 4096
            and extent % 8 == 0
            and got["sh_addr"] + got["sh_size"] <= initialized,
            "fixture state containment differs",
        )
        require(
            0x10020000 < flash_end and flash_end + initialized <= 0x1002C000,
            "flash reservation differs",
        )
        cells = state_relocations(elf, got, initialized)
        return {
            "symbols": {name: symbol["st_value"] for name, symbol in symbols.items()},
            "sections": [
                (section["sh_addr"], section.data())
                for section in elf.iter_sections()
                if section["sh_flags"] & 2 and section["sh_type"] != "SHT_NOBITS"
            ],
            "relocations": cells,
            "got_offset": got["sh_addr"],
            "state_size": extent,
            "slot_width": width,
            "text_image_size": flash_end - 0x10020000,
            "initialized_size": initialized,
            "flash_size": flash_end - 0x10020000 + initialized,
        }
