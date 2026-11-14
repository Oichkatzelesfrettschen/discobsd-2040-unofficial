"""Resolve explicit shared-libc API roots through compiled ARM archive members."""

import argparse
import collections
import hashlib
import io
import json
import re
import struct
import subprocess
from pathlib import Path

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection

SYMBOL = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
MEMBER = re.compile(r"[A-Za-z_][A-Za-z0-9_.-]*\.o\Z")


class ClosureError(ValueError):
    """The API or provider ownership cannot close over the supplied artifacts."""


def require(condition, message):
    if not condition:
        raise ClosureError(message)


def command(argv):
    try:
        result = subprocess.run([str(value) for value in argv], capture_output=True,
                                check=False, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ClosureError(f"archive command fails: {error}") from error
    require(result.returncode == 0 and not result.stderr,
            "archive command fails: " + result.stderr.decode(errors="replace"))
    return result.stdout


def comdat_groups(elf, table, member_name):
    """Normalize owned storage and relocation identities before accepting duplicates."""
    owners, groups = {}, {}
    for group_index, section in enumerate(elf.iter_sections()):
        if section["sh_type"] != "SHT_GROUP":
            continue
        require(section["sh_link"] == elf.get_section_index(".symtab"),
                f"{member_name}: group uses an unsupported symbol table")
        data = section.data()
        require(len(data) >= 8 and len(data) % 4 == 0,
                f"{member_name}: malformed COMDAT group")
        words = struct.unpack("<" + "I" * (len(data) // 4), data)
        require(words[0] == 1, f"{member_name}: unsupported section group flags")
        require(section["sh_flags"] == 0 and section["sh_entsize"] == 4 and
                0 < section["sh_info"] < table.num_symbols(),
                f"{member_name}: malformed COMDAT header")
        signature = table.get_symbol(section["sh_info"]).name
        require(signature and signature not in groups,
                f"{member_name}: invalid or duplicate COMDAT signature")
        indexes = words[1:]
        require(len(indexes) == len(set(indexes)), f"{member_name}: repeated group member")
        names = set()
        for index in indexes:
            require(group_index < index < elf.num_sections() and index not in owners,
                    f"{member_name}: invalid or overlapping group ownership")
            target = elf.get_section(index)
            require(target["sh_flags"] & 0x200,
                    f"{member_name}: group member lacks SHF_GROUP")
            require(target.name not in names, f"{member_name}: ambiguous group section names")
            names.add(target.name)
            owners[index] = signature
        groups[signature] = {"indexes": indexes}
    for index, section in enumerate(elf.iter_sections()):
        require(not section["sh_flags"] & 0x200 or index in owners,
                f"{member_name}: orphan SHF_GROUP section")
        if isinstance(section, RelocationSection):
            target_owner = owners.get(section["sh_info"])
            require(target_owner is None or owners.get(index) == target_owner,
                    f"{member_name}: relocation bypasses COMDAT ownership")
            require(0 < section["sh_link"] < elf.num_sections(),
                    f"{member_name}: invalid relocation symbol table")
            relocation_table = elf.get_section(section["sh_link"])
            require(isinstance(relocation_table, SymbolTableSection),
                    f"{member_name}: invalid relocation symbol table")
            for relocation in section.iter_relocations():
                require(relocation["r_info_sym"] < relocation_table.num_symbols(),
                        f"{member_name}: invalid relocation symbol index")
                symbol = relocation_table.get_symbol(relocation["r_info_sym"])
                symbol_owner = owners.get(symbol["st_shndx"])
                require(symbol["st_info"]["bind"] != "STB_LOCAL" or symbol_owner is None or
                        owners.get(section["sh_info"]) == symbol_owner,
                        f"{member_name}: external reference to a COMDAT local symbol")
        for target in [section["sh_link"],
                       *([section["sh_info"]] if section["sh_flags"] & 0x40 else [])]:
            require(target not in owners or owners.get(index) == owners[target],
                    f"{member_name}: external section reference to COMDAT storage")

    def symbol_identity(symbol, signature):
        index = symbol["st_shndx"]
        identity = [symbol.name, symbol["st_info"]["bind"], symbol["st_info"]["type"],
                    symbol["st_other"]["visibility"], symbol["st_size"]]
        if isinstance(index, int) and owners.get(index) == signature:
            return identity + ["group", elf.get_section(index).name, symbol["st_value"]]
        if symbol["st_info"]["bind"] == "STB_LOCAL":
            # A local outside the group belongs to this object even when its name matches.
            return identity + ["local-owner", member_name, index, symbol["st_value"]]
        return identity + ["external-name"]

    for signature, group in groups.items():
        normalized, allocated = [], collections.Counter()
        for index in group["indexes"]:
            section = elf.get_section(index)
            row = {"name": section.name, "type": section["sh_type"],
                   "flags": section["sh_flags"], "alignment": section["sh_addralign"],
                   "entry_size": section["sh_entsize"], "size": section["sh_size"]}
            if isinstance(section, RelocationSection):
                require(section["sh_info"] in group["indexes"] and
                        section["sh_link"] == elf.get_section_index(".symtab"),
                        f"{member_name}: relocation leaves its COMDAT group")
                row["target"] = elf.get_section(section["sh_info"]).name
                row["relocations"] = []
                for relocation in section.iter_relocations():
                    entry = [relocation["r_offset"], relocation["r_info_type"],
                             symbol_identity(table.get_symbol(relocation["r_info_sym"]),
                                             signature)]
                    if section.is_RELA():
                        entry.append(relocation["r_addend"])
                    row["relocations"].append(entry)
            else:
                require(section["sh_type"] in ("SHT_PROGBITS", "SHT_NOBITS") and
                        section["sh_link"] == 0 and section["sh_info"] == 0,
                        f"{member_name}: unsupported COMDAT storage metadata")
                row["bytes_sha256"] = (hashlib.sha256(section.data()).hexdigest()
                                       if section["sh_type"] != "SHT_NOBITS" else None)
            if section["sh_flags"] & 2:
                allocated[section.name] += section["sh_size"]
            normalized.append(row)
        exports = [symbol_identity(symbol, signature) for symbol in table.iter_symbols()
                   if isinstance(symbol["st_shndx"], int) and
                   owners.get(symbol["st_shndx"]) == signature and
                   symbol["st_info"]["bind"] in ("STB_GLOBAL", "STB_WEAK")]
        payload = {"sections": sorted(normalized, key=lambda row: row["name"]),
                   "definitions": sorted(exports)}
        group["sha256"] = hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()
        group["input_section_bytes"] = dict(sorted(allocated.items()))
        del group["indexes"]
    return owners, groups


def read_member(name, content):
    try:
        elf = ELFFile(io.BytesIO(content))
        require(elf.elfclass == 32 and elf.little_endian and elf["e_machine"] == "EM_ARM"
                and elf["e_type"] == "ET_REL", f"{name}: expected ARM ELF32 relocatable")
        table = elf.get_section_by_name(".symtab")
        require(table is not None, f"{name}: symbol table is absent")
        group_owners, groups = comdat_groups(elf, table, name)
        definitions = {}
        strong, weak = set(), set()
        for symbol in table.iter_symbols():
            binding = symbol["st_info"]["bind"]
            if binding not in ("STB_GLOBAL", "STB_WEAK") or not symbol.name:
                continue
            require(SYMBOL.fullmatch(symbol.name), f"{name}: invalid global symbol {symbol.name}")
            index = symbol["st_shndx"]
            if index == "SHN_UNDEF":
                (weak if binding == "STB_WEAK" else strong).add(symbol.name)
                continue
            require(index != "SHN_COMMON", f"{name}: common symbol lacks sole owner: {symbol.name}")
            if index == "SHN_ABS":
                kind, mutable = "absolute", False
            else:
                require(isinstance(index, int), f"{name}: unsupported symbol section")
                section = elf.get_section(index)
                require(section["sh_flags"] & 2, f"{name}: global symbol is not allocated")
                symbol_type = symbol["st_info"]["type"]
                require(symbol_type in ("STT_FUNC", "STT_OBJECT", "STT_NOTYPE"),
                        f"{name}: unsupported symbol type for {symbol.name}: {symbol_type}")
                require(symbol_type != "STT_FUNC" or section["sh_flags"] & 4,
                        f"{name}: function lacks executable storage: {symbol.name}")
                kind = ("object" if symbol_type == "STT_OBJECT" else
                        "function" if section["sh_flags"] & 4 else "object")
                mutable = bool(section["sh_flags"] & 1)
            require(symbol.name not in definitions, f"{name}: duplicate symbol {symbol.name}")
            definitions[symbol.name] = {"kind": kind, "mutable": mutable,
                                        "binding": binding, "size": symbol["st_size"]}
            if index in group_owners:
                signature = group_owners[index]
                definitions[symbol.name]["comdat"] = {
                    "signature": signature, "sha256": groups[signature]["sha256"]}
        sections = collections.Counter()
        for section in elf.iter_sections():
            if section["sh_flags"] & 2:
                sections[section.name] += section["sh_size"]
        return {"sha256": hashlib.sha256(content).hexdigest(), "definitions": definitions,
                "strong_imports": sorted(strong), "weak_imports": sorted(weak),
                "input_section_bytes": dict(sorted(sections.items())), "comdat_groups": groups}
    except (ELFError, KeyError, IndexError) as error:
        raise ClosureError(f"{name}: malformed ELF: {error}") from error


def read_archive(path, ar):
    path = path.resolve()
    content = path.read_bytes()
    require(content.startswith(b"!<arch>\n"), "expected regular embedded archive")
    before = hashlib.sha256(content).hexdigest()
    try:
        names = command([ar, "t", path]).decode("ascii").splitlines()
    except UnicodeError as error:
        raise ClosureError("archive member names must be ASCII") from error
    require(names, "archive contains no object members")
    require(len(names) == len(set(names)), "duplicate archive member names")
    require(len(names) == len({name.casefold() for name in names}),
            "archive member names collide on a case-insensitive filesystem")
    for name in names:
        require(MEMBER.fullmatch(name), f"unsafe archive member name: {name}")
    members = {name: read_member(name, command([ar, "p", path, name])) for name in names}
    require(hashlib.sha256(path.read_bytes()).hexdigest() == before,
            "archive changes during inspection")
    providers = collections.defaultdict(list)
    for name, member in members.items():
        for symbol in member["definitions"]:
            providers[symbol].append(name)
    return {"schema": 1, "archive_sha256": before, "members": members,
            "providers": dict(sorted(providers.items()))}


def read_contract(document):
    require(isinstance(document, dict) and set(document) ==
            {"schema", "roots", "external_symbols"}, "invalid root contract fields")
    require(type(document["schema"]) is int and document["schema"] == 1,
            "unsupported root contract schema")
    roots, external = document["roots"], document["external_symbols"]
    require(isinstance(roots, list) and roots, "root contract requires explicit API roots")
    require(isinstance(external, list), "external symbol declarations must form a list")
    for rows, fields in ((roots, {"symbol", "kind", "reason"}),
                         (external, {"symbol", "owner", "reason"})):
        seen = set()
        for row in rows:
            require(isinstance(row, dict) and set(row) == fields, "invalid symbol declaration")
            name = row["symbol"]
            require(isinstance(name, str) and SYMBOL.fullmatch(name), "invalid symbol name")
            require(name not in seen, f"duplicate symbol declaration: {name}")
            seen.add(name)
            require(isinstance(row["reason"], str) and row["reason"].strip(),
                    f"symbol {name} requires a reason")
            if "kind" in row:
                require(row["kind"] in ("function", "object"), f"invalid API kind: {name}")
            else:
                require(isinstance(row["owner"], str) and SYMBOL.fullmatch(row["owner"]),
                        f"invalid external owner: {name}")
    require(not ({row["symbol"] for row in roots} & {row["symbol"] for row in external}),
            "an external owner cannot satisfy a public API root")
    return {"schema": 1, "roots": sorted(roots, key=lambda row: row["symbol"]),
            "external_symbols": sorted(external, key=lambda row: row["symbol"])}


def provider(graph, symbol, candidates=None):
    names = graph["providers"].get(symbol, [])
    if candidates is not None:
        names = [name for name in names if name in candidates]
    strong = [name for name in names
              if graph["members"][name]["definitions"][symbol]["binding"] == "STB_GLOBAL"]
    choices = strong or names
    if len(choices) > 1:
        identities = [graph["members"][name]["definitions"][symbol].get("comdat")
                      for name in choices]
        require(identities[0] is not None and all(item == identities[0] for item in identities),
                f"conflicting providers for {symbol}: {', '.join(choices)}")
        choices = sorted(choices)
    return choices[0] if choices else None


def external_names(graph, specification):
    names = {row["symbol"] for row in specification["external_symbols"]}
    require(not names.intersection(graph["providers"]),
            "external ownership conflicts with an archive definition")
    return names


def verify(graph, specification, selection):
    require(isinstance(selection, list) and all(isinstance(name, str) for name in selection),
            "member selection must be a list of names")
    require(len(selection) == len(set(selection)), "duplicate selected member")
    selected = set(selection)
    require(selected.issubset(graph["members"]), "selection names an absent archive member")
    external = external_names(graph, specification)
    for root in specification["roots"]:
        member = provider(graph, root["symbol"], selected)
        require(member is not None, f"required API {root['symbol']} is absent from selection")
        require(graph["members"][member]["definitions"][root["symbol"]]["kind"] == root["kind"],
                f"API kind differs for {root['symbol']}")
    outside, optional = set(), set()
    sections, discarded = collections.Counter(), collections.Counter()
    groups = {}
    for name in sorted(selected):
        member = graph["members"][name]
        for symbol in member["definitions"]:
            provider(graph, symbol, selected)
        for symbol in member["strong_imports"]:
            if symbol in external:
                outside.add(symbol)
            else:
                require(provider(graph, symbol, selected) is not None,
                        f"unresolved strong import {symbol} from {name}")
        for symbol in member["weak_imports"]:
            if provider(graph, symbol, selected) is None:
                optional.add(symbol)
        sections.update(member["input_section_bytes"])
        for signature, group in member.get("comdat_groups", {}).items():
            if signature in groups:
                require(group["sha256"] == groups[signature]["sha256"],
                        "conflicting COMDAT groups: " + signature)
                discarded.update(group["input_section_bytes"])
            else:
                groups[signature] = group
    return {"schema": 1, "archive_sha256": graph["archive_sha256"],
            "contract": specification, "members": sorted(selected),
            "external_imports": sorted(outside), "unresolved_weak": sorted(optional),
            "input_section_bytes": dict(sorted(sections.items())),
            "coalesced_input_section_bytes": {
                name: size - discarded[name] for name, size in sorted(sections.items())},
            "comdat_groups": groups}


def resolve(graph, specification):
    external = external_names(graph, specification)
    pending = [root["symbol"] for root in specification["roots"]]
    selected = set()
    while pending:
        symbol = pending.pop()
        if symbol in external:
            continue
        name = provider(graph, symbol)
        require(name is not None, f"unresolved strong import {symbol}")
        if name in selected:
            continue
        selected.add(name)
        pending.extend(graph["members"][name]["strong_imports"])
    return verify(graph, specification, sorted(selected))


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        require(name not in result, f"duplicate JSON field: {name}")
        result[name] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--ar", default="arm-none-eabi-ar")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--describe", action="store_true")
    mode.add_argument("--contract", type=Path)
    parser.add_argument("--members", type=Path,
                        help="verify an explicit JSON member list instead of computing closure")
    args = parser.parse_args()
    if args.members and args.describe:
        parser.error("--members requires --contract")
    try:
        graph = read_archive(args.archive, args.ar)
        if args.describe:
            report = graph
        else:
            specification = read_contract(json.loads(args.contract.read_text(),
                                                     object_pairs_hook=unique_object))
            report = (verify(graph, specification, json.loads(args.members.read_text()))
                      if args.members else resolve(graph, specification))
        print(json.dumps(report, indent=2, sort_keys=True))
    except (OSError, ValueError) as error:
        parser.exit(1, f"FAIL shared-libc closure: {error}\n")


if __name__ == "__main__":
    main()
