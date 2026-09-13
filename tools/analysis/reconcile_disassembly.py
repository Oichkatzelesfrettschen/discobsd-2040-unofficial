#!/usr/bin/env python3
"""Cross-check Capstone, GNU objdump, LLVM objdump, and LIEF on one ELF.

Loads an ELF's executable sections and ARM mapping symbols ($a/$t/$d),
classifies Thumb/ARM/data byte ranges, decodes the Thumb ranges with
Capstone, and cross-checks the resulting instruction-start address set
against `arm-none-eabi-objdump` and `llvm-objdump`, plus a LIEF section and
mapping-symbol reparse. The RP2040 stage-2 bootloader (.boot2, a fixed
0xCC-byte code prologue trailed by data) is treated as a special case since
it carries no mapping symbols of its own.

Inputs: the linked ELF (the built kernel at
sys/arch/rp2040/compile/PICO/unix) and a second ELF wrapping just the boot2
image for the boot-code cross-check. Third-party modules: capstone, lief,
and pyelftools (elftools). Requires arm-none-eabi-objdump and llvm-objdump
on PATH.
"""

import collections
import json
import pathlib
import re
import subprocess
import sys

import capstone
import lief
from elftools.elf.elffile import ELFFile


MAPPING_SYMBOL_PATTERN = re.compile(r"^\$([atd])(?:\..*)?$")
DISASSEMBLY_ADDRESS_PATTERN = re.compile(r"^\s*([0-9a-fA-F]+):\s")
BOOT_CODE_START = 0x10000000
BOOT_CODE_END = 0x100000CC


def run_command(command):
    completed_process = subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed_process.stdout


def load_elf_metadata(elf_path):
    with elf_path.open("rb") as elf_stream:
        elf_file = ELFFile(elf_stream)
        symbol_table = elf_file.get_section_by_name(".symtab")
        executable_sections = {}
        mapping_symbols_by_section = collections.defaultdict(list)

        for section_index, section in enumerate(elf_file.iter_sections()):
            if int(section["sh_flags"]) & 0x4:
                executable_sections[section_index] = {
                    "index": section_index,
                    "name": section.name,
                    "address": int(section["sh_addr"]),
                    "size": int(section["sh_size"]),
                    "data": section.data(),
                }

        for symbol in symbol_table.iter_symbols():
            mapping_match = MAPPING_SYMBOL_PATTERN.fullmatch(symbol.name)
            section_index = symbol["st_shndx"]
            if mapping_match is None or not isinstance(section_index, int):
                continue
            if section_index not in executable_sections:
                continue
            mapping_symbols_by_section[section_index].append(
                (int(symbol["st_value"]), mapping_match.group(1), symbol.name)
            )

    return executable_sections, mapping_symbols_by_section


def mapping_ranges(executable_sections, mapping_symbols_by_section):
    classified_ranges = []
    for section_index, section in executable_sections.items():
        section_start = section["address"]
        section_end = section_start + section["size"]
        section_markers = sorted(mapping_symbols_by_section[section_index])
        if section["name"] == ".boot2":
            classified_ranges.append(
                {
                    "section": ".boot2",
                    "kind": "t",
                    "start": BOOT_CODE_START,
                    "end": BOOT_CODE_END,
                }
            )
            classified_ranges.append(
                {
                    "section": ".boot2",
                    "kind": "d",
                    "start": BOOT_CODE_END,
                    "end": section_end,
                }
            )
            continue
        if not section_markers:
            raise RuntimeError(f"executable section {section['name']} lacks mapping symbols")
        if section_markers[0][0] != section_start:
            raise RuntimeError(
                f"first mapping symbol for {section['name']} is not at section start"
            )

        distinct_markers = []
        for marker_address, marker_kind, marker_name in section_markers:
            if distinct_markers and marker_address == distinct_markers[-1][0]:
                if marker_kind != distinct_markers[-1][1]:
                    raise RuntimeError(
                        f"conflicting mapping symbols at 0x{marker_address:x}"
                    )
                continue
            distinct_markers.append((marker_address, marker_kind, marker_name))

        section_ranges = []
        for marker_index, (range_start, range_kind, _) in enumerate(distinct_markers):
            if marker_index + 1 < len(distinct_markers):
                range_end = distinct_markers[marker_index + 1][0]
            else:
                range_end = section_end
            if range_start < section_start or range_end > section_end:
                raise RuntimeError(f"mapping symbol escapes section {section['name']}")
            if range_start != range_end:
                section_ranges.append(
                    {
                        "section": section["name"],
                        "kind": range_kind,
                        "start": range_start,
                        "end": range_end,
                    }
                )

        for section_range in section_ranges:
            if (
                classified_ranges
                and classified_ranges[-1]["section"] == section_range["section"]
                and classified_ranges[-1]["kind"] == section_range["kind"]
                and classified_ranges[-1]["end"] == section_range["start"]
            ):
                classified_ranges[-1]["end"] = section_range["end"]
            else:
                classified_ranges.append(section_range)

    return classified_ranges


def bytes_for_range(executable_sections, classified_range):
    matching_section = next(
        section
        for section in executable_sections.values()
        if section["name"] == classified_range["section"]
    )
    range_offset = classified_range["start"] - matching_section["address"]
    range_size = classified_range["end"] - classified_range["start"]
    return matching_section["data"][range_offset : range_offset + range_size]


def decode_with_capstone(executable_sections, thumb_ranges):
    decoder = capstone.Cs(
        capstone.CS_ARCH_ARM,
        capstone.CS_MODE_THUMB
        | capstone.CS_MODE_LITTLE_ENDIAN
        | capstone.CS_MODE_MCLASS,
    )
    decoder.detail = False
    instructions = []
    undecoded_ranges = []
    for thumb_range in thumb_ranges:
        range_bytes = bytes_for_range(executable_sections, thumb_range)
        range_instructions = list(decoder.disasm(range_bytes, thumb_range["start"]))
        consumed_bytes = sum(instruction.size for instruction in range_instructions)
        if consumed_bytes != len(range_bytes):
            undecoded_ranges.append(
                {
                    "start": thumb_range["start"],
                    "end": thumb_range["end"],
                    "consumed": consumed_bytes,
                    "expected": len(range_bytes),
                }
            )
        instructions.extend(range_instructions)
    return instructions, undecoded_ranges


def addresses_from_disassembly(disassembly_text, address_adjustment=0):
    addresses = set()
    for line in disassembly_text.splitlines():
        address_match = DISASSEMBLY_ADDRESS_PATTERN.match(line)
        if address_match is not None:
            addresses.add(int(address_match.group(1), 16) + address_adjustment)
    return addresses


def address_in_ranges(address, ranges):
    return any(item["start"] <= address < item["end"] for item in ranges)


def cross_check_disassemblers(elf_path, boot_wrapper_path, thumb_ranges):
    non_boot_ranges = [item for item in thumb_ranges if item["section"] != ".boot2"]
    # GNU's selective -d mode prints the two-byte mapping-symbol range at
    # 0x10000f02 as raw bytes because the range lies inside an STT_OBJECT.
    # -D emits the mapped Thumb instruction; the authoritative range filter
    # below excludes every decoded data address.
    gnu_main_text = run_command(
        ["arm-none-eabi-objdump", "-D", "--disassemble-zeroes", str(elf_path)]
    )
    llvm_main_text = run_command(
        ["llvm-objdump", "--disassemble", "--disassemble-zeroes", str(elf_path)]
    )
    gnu_boot_text = run_command(
        [
            "arm-none-eabi-objdump",
            "-d",
            "-Mforce-thumb",
            "--disassemble-zeroes",
            "--start-address=0",
            f"--stop-address={BOOT_CODE_END - BOOT_CODE_START:#x}",
            str(boot_wrapper_path),
        ]
    )
    llvm_boot_text = run_command(
        [
            "llvm-objdump",
            "--triple=thumbv6m-none-eabi",
            "--disassemble",
            "--disassemble-zeroes",
            "--start-address=0",
            f"--stop-address={BOOT_CODE_END - BOOT_CODE_START:#x}",
            str(boot_wrapper_path),
        ]
    )

    gnu_main_addresses = {
        address
        for address in addresses_from_disassembly(gnu_main_text)
        if address_in_ranges(address, non_boot_ranges)
    }
    # A full GNU -D pass can begin an odd-addressed STT_OBJECT at that object's
    # byte address before it encounters a nested $t marker.  Re-run only a
    # range whose first decoded address is absent or odd, with the mapping
    # boundary supplied explicitly.
    for thumb_range in non_boot_ranges:
        range_addresses = {
            address
            for address in gnu_main_addresses
            if thumb_range["start"] <= address < thumb_range["end"]
        }
        if thumb_range["start"] in range_addresses and all(
            address % 2 == 0 for address in range_addresses
        ):
            continue
        restricted_text = run_command(
            [
                "arm-none-eabi-objdump",
                "-D",
                "-Mforce-thumb",
                "--disassemble-zeroes",
                f"--start-address={thumb_range['start']:#x}",
                f"--stop-address={thumb_range['end']:#x}",
                str(elf_path),
            ]
        )
        gnu_main_addresses.difference_update(range_addresses)
        gnu_main_addresses.update(
            address
            for address in addresses_from_disassembly(restricted_text)
            if thumb_range["start"] <= address < thumb_range["end"]
        )
    llvm_main_addresses = {
        address
        for address in addresses_from_disassembly(llvm_main_text)
        if address_in_ranges(address, non_boot_ranges)
    }
    boot_range = [
        {
            "start": BOOT_CODE_START,
            "end": BOOT_CODE_END,
        }
    ]
    gnu_boot_addresses = {
        address
        for address in addresses_from_disassembly(gnu_boot_text, BOOT_CODE_START)
        if address_in_ranges(address, boot_range)
    }
    llvm_boot_addresses = {
        address
        for address in addresses_from_disassembly(llvm_boot_text, BOOT_CODE_START)
        if address_in_ranges(address, boot_range)
    }
    return {
        "gnu": gnu_main_addresses | gnu_boot_addresses,
        "llvm": llvm_main_addresses | llvm_boot_addresses,
        "gnu_boot": gnu_boot_addresses,
        "llvm_boot": llvm_boot_addresses,
    }


def validate_lief(elf_path, executable_sections, mapping_symbols_by_section):
    lief_binary = lief.parse(str(elf_path))
    lief_sections = {
        section.name: {
            "address": int(section.virtual_address),
            "size": int(section.size),
            "content": bytes(section.content),
        }
        for section in lief_binary.sections
    }
    section_mismatches = []
    for section in executable_sections.values():
        lief_section = lief_sections[section["name"]]
        expected = (section["address"], section["size"], section["data"])
        observed = (
            lief_section["address"],
            lief_section["size"],
            lief_section["content"],
        )
        if expected != observed:
            section_mismatches.append(section["name"])

    pyelftools_mapping_symbols = {
        (section_index, address, kind)
        for section_index, symbols in mapping_symbols_by_section.items()
        for address, kind, _ in symbols
    }
    lief_mapping_symbols = {
        (int(symbol.shndx), int(symbol.value), mapping_match.group(1))
        for symbol in lief_binary.symbols
        if (mapping_match := MAPPING_SYMBOL_PATTERN.fullmatch(symbol.name)) is not None
        and int(symbol.shndx) in executable_sections
    }
    return {
        "section_mismatches": section_mismatches,
        "pyelftools_mapping_count": len(pyelftools_mapping_symbols),
        "lief_mapping_count": len(lief_mapping_symbols),
        "mapping_only_pyelftools": sorted(pyelftools_mapping_symbols - lief_mapping_symbols),
        "mapping_only_lief": sorted(lief_mapping_symbols - pyelftools_mapping_symbols),
    }


def summarize(elf_path, boot_wrapper_path):
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    classified_ranges = mapping_ranges(executable_sections, mapping_symbols_by_section)
    thumb_ranges = [item for item in classified_ranges if item["kind"] == "t"]
    capstone_instructions, undecoded_ranges = decode_with_capstone(
        executable_sections, thumb_ranges
    )
    decoder_addresses = cross_check_disassemblers(
        elf_path, boot_wrapper_path, thumb_ranges
    )
    capstone_addresses = {instruction.address for instruction in capstone_instructions}
    width_histogram = collections.Counter(
        instruction.size for instruction in capstone_instructions
    )
    mnemonic_histogram = collections.Counter(
        instruction.mnemonic for instruction in capstone_instructions
    )
    section_thumb_bytes = collections.Counter()
    for thumb_range in thumb_ranges:
        section_thumb_bytes[thumb_range["section"]] += (
            thumb_range["end"] - thumb_range["start"]
        )

    comparison = {}
    for decoder_name in ("gnu", "llvm"):
        decoder_set = decoder_addresses[decoder_name]
        comparison[decoder_name] = {
            "count": len(decoder_set),
            "missing_from_decoder": [
                f"0x{address:08x}" for address in sorted(capstone_addresses - decoder_set)
            ],
            "extra_in_decoder": [
                f"0x{address:08x}" for address in sorted(decoder_set - capstone_addresses)
            ],
        }

    lief_validation = validate_lief(
        elf_path, executable_sections, mapping_symbols_by_section
    )
    report = {
        "executable_sections": [
            {
                "name": section["name"],
                "address": f"0x{section['address']:08x}",
                "size": section["size"],
                "mapping_symbols": len(mapping_symbols_by_section[section_index]),
            }
            for section_index, section in executable_sections.items()
        ],
        "classified_range_count": len(classified_ranges),
        "thumb_range_count": len(thumb_ranges),
        "thumb_bytes_by_section": dict(sorted(section_thumb_bytes.items())),
        "instruction_count": len(capstone_instructions),
        "instruction_bytes": sum(
            instruction.size for instruction in capstone_instructions
        ),
        "width_histogram": dict(sorted(width_histogram.items())),
        "top_mnemonics": mnemonic_histogram.most_common(30),
        "capstone_undecoded_ranges": undecoded_ranges,
        "decoder_comparison": comparison,
        "boot_decoder_counts": {
            "gnu": len(decoder_addresses["gnu_boot"]),
            "llvm": len(decoder_addresses["llvm_boot"]),
        },
        "lief_validation": lief_validation,
    }
    return report


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: reconcile_disassembly.py ELF BOOT_WRAPPER_ELF")
    elf_path = pathlib.Path(sys.argv[1])
    boot_wrapper_path = pathlib.Path(sys.argv[2])
    report = summarize(elf_path, boot_wrapper_path)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
