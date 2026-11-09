#!/usr/bin/env python3
"""Measure the LARGE image and emulate the linked next-fit allocator."""

import argparse
import math
import struct
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_LITTLE_ENDIAN, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_LITTLE_ENDIAN, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
    UC_ARM_REG_SP,
)

FLASH_BASE = 0x10000000
FLASH_SIZE = 0x00200000
SRAM_BASE = 0x20000000
SRAM_SIZE = 0x00050000
MAP_ADDRESS = 0x20001000
ENTRY_ADDRESS = 0x20001100
CURSOR_ADDRESS = 0x20001200
OUTPUT_ADDRESS = 0x20001300
STACK_ADDRESS = 0x20022000
STOP_ADDRESS = 0x2003F000


def get_symbol(elf_file, name):
    symbols = elf_file.get_section_by_name(".symtab").get_symbol_by_name(name)
    if not symbols:
        raise RuntimeError(f"missing ELF symbol: {name}")
    return int(symbols[0]["st_value"]) & ~1, int(symbols[0]["st_size"])


def load_segments(emulator, elf_file):
    for segment in elf_file.iter_segments():
        if segment["p_type"] == "PT_LOAD":
            emulator.mem_write(int(segment["p_vaddr"]), segment.data())


def write_map(emulator, entries):
    encoded = b"".join(struct.pack("<HH", size, address) for size, address in entries)
    emulator.mem_write(ENTRY_ADDRESS, encoded + struct.pack("<HH", 0, 0))
    emulator.mem_write(
        MAP_ADDRESS,
        struct.pack("<III", ENTRY_ADDRESS, ENTRY_ADDRESS + 29 * 4, 0),
    )


def emulate_allocation(
    elf_path,
    function_address,
    entries,
    cursor,
    data_blocks,
    stack_blocks,
    user_blocks,
):
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_LITTLE_ENDIAN)
    emulator.mem_map(FLASH_BASE, FLASH_SIZE)
    emulator.mem_map(SRAM_BASE, SRAM_SIZE)
    with elf_path.open("rb") as elf_stream:
        load_segments(emulator, ELFFile(elf_stream))
    write_map(emulator, entries)
    emulator.mem_write(CURSOR_ADDRESS, struct.pack("<I", cursor))
    emulator.mem_write(OUTPUT_ADDRESS, b"\xa5" * 12)
    emulator.mem_write(
        STACK_ADDRESS,
        struct.pack("<III", 4, CURSOR_ADDRESS, OUTPUT_ADDRESS),
    )
    emulator.mem_write(STOP_ADDRESS, b"\x00\xbe")
    emulator.reg_write(UC_ARM_REG_SP, STACK_ADDRESS)
    emulator.reg_write(UC_ARM_REG_LR, STOP_ADDRESS | 1)
    emulator.reg_write(UC_ARM_REG_R0, MAP_ADDRESS)
    emulator.reg_write(UC_ARM_REG_R1, data_blocks)
    emulator.reg_write(UC_ARM_REG_R2, stack_blocks)
    emulator.reg_write(UC_ARM_REG_R3, user_blocks)
    instruction_count = 0

    def stop_after_return(active_emulator, address, size, unused):
        nonlocal instruction_count
        del size, unused
        instruction_count += 1
        if address == STOP_ADDRESS:
            active_emulator.emu_stop()

    emulator.hook_add(UC_HOOK_CODE, stop_after_return)
    emulator.emu_start(function_address | 1, STOP_ADDRESS + 2, count=100_000)
    result = emulator.reg_read(UC_ARM_REG_R0)
    final_cursor = struct.unpack("<I", emulator.mem_read(CURSOR_ADDRESS, 4))[0]
    outputs = struct.unpack("<III", emulator.mem_read(OUTPUT_ADDRESS, 12))
    final_entries = []
    for entry_index in range(29):
        size, address = struct.unpack("<HH", emulator.mem_read(ENTRY_ADDRESS + entry_index * 4, 4))
        if size == 0:
            break
        final_entries.append((size, address))
    return result, final_cursor, outputs, final_entries, instruction_count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kernel_elf", type=Path)
    parser.add_argument("bigtest_aout", type=Path)
    arguments = parser.parse_args()
    with arguments.kernel_elf.open("rb") as elf_stream:
        elf_file = ELFFile(elf_stream)
        allocator_address, allocator_size = get_symbol(elf_file, "malloc3_contiguous_next")
        text_section = elf_file.get_section_by_name(".text")
        allocator_offset = allocator_address - int(text_section["sh_addr"])
        allocator_bytes = text_section.data()[allocator_offset : allocator_offset + allocator_size]
    instructions = list(
        Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN).disasm(
            allocator_bytes, allocator_address
        )
    )
    with arguments.bigtest_aout.open("rb") as aout_stream:
        header = struct.unpack("<8I", aout_stream.read(32))
    midmag, text_bytes, data_bytes, bss_bytes = header[:4]
    observed_data_bytes = 159_744
    observed_stack_bytes = 2_244
    user_bytes = 3_072
    data_blocks = math.ceil((observed_data_bytes - text_bytes) / 1_024)
    stack_blocks = math.ceil(observed_stack_bytes / 1_024)
    user_blocks = math.ceil(user_bytes / 1_024)
    image_blocks = (data_blocks + stack_blocks + user_blocks + 3) & ~3
    programmed_pages = (
        math.ceil((observed_data_bytes - text_bytes) / 256)
        + math.ceil(observed_stack_bytes / 256)
        + math.ceil(user_bytes / 256)
    )
    recorded_map = [(120, 4), (4, 144), (28, 324), (12, 372)]
    failed_cursors = []
    changed_failures = []
    for cursor in range(4, 384, 4):
        result = emulate_allocation(
            arguments.kernel_elf,
            allocator_address,
            recorded_map,
            cursor,
            data_blocks,
            stack_blocks,
            user_blocks,
        )
        if result[0] == 0:
            failed_cursors.append(cursor)
        if (
            result[0] != 0
            or result[1] != cursor
            or result[2] != (0xA5A5A5A5,) * 3
            or result[3] != recorded_map
        ):
            changed_failures.append(cursor)
    positive = emulate_allocation(
        arguments.kernel_elf,
        allocator_address,
        [(164, 4)],
        4,
        data_blocks,
        stack_blocks,
        user_blocks,
    )
    print(
        f"a.out header: midmag=0x{midmag:08x} text={text_bytes} data={data_bytes} bss={bss_bytes}"
    )
    print(
        f"observed image: data_after_text={observed_data_bytes - text_bytes} "
        f"blocks={data_blocks}+{stack_blocks}+{user_blocks} "
        f"aligned_blocks={image_blocks} sectors={image_blocks // 4} "
        f"program_pages={programmed_pages} exchange_peak={216 + 2 * image_blocks}"
    )
    print(
        f"capstone: address=0x{allocator_address:08x} bytes={allocator_size} "
        f"instructions={len(instructions)} first={instructions[0].mnemonic} "
        f"last={instructions[-1].mnemonic}"
    )
    print(
        f"unicorn recorded map: failed={len(failed_cursors)}/95 "
        f"state_changes_on_failure={len(changed_failures)}"
    )
    print(
        f"unicorn positive map: result={positive[0]} cursor={positive[1]} "
        f"outputs={positive[2]} map={positive[3]} instructions={positive[4]}"
    )


if __name__ == "__main__":
    main()
