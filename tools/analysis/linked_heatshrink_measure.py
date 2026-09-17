#!/usr/bin/env python3
"""Measure man-page compression through the linked RP2040 codec.

The harness loads the production kernel ELF into Unicorn, calls the linked
heatshrink encoder and decoder through their ARM EABI entry points, and keeps
all derived streams in host memory.  It neither compiles code nor writes the
source checkout.

Inputs: the built kernel ELF at sys/arch/rp2040/compile/PICO/unix (produced
by `bmake MACHINE=rp2040 kernel`) and the man1 pages it links heatshrink
against.  Third-party modules: pyelftools (elftools) and unicorn.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
    UC_ARM_REG_SP,
)

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_TREE_ROOT = SCRIPT_DIR.parent.parent
PAGE_NAMES = ("sh", "ed", "awk", "sed", "grep", "find", "cc")

FLASH_BASE = 0x10000000
FLASH_SIZE = 0x00200000
RAM_BASE = 0x20000000
RAM_SIZE = 0x00100000
SCRATCH_BASE = 0x21000000
SCRATCH_SIZE = 0x00800000

INPUT_ADDRESS = SCRATCH_BASE
OUTPUT_ADDRESS = SCRATCH_BASE + 0x00100000
ENCODER_ADDRESS = SCRATCH_BASE + 0x00300000
DECODER_ADDRESS = SCRATCH_BASE + 0x00301000
COUNT_ADDRESS = SCRATCH_BASE + 0x00302000
STACK_TOP = SCRATCH_BASE + 0x007FF000
RETURN_ADDRESS = SCRATCH_BASE + 0x007FE000
TRANSFER_BUFFER_SIZE = 0x00080000
MAX_CALL_INSTRUCTIONS = 100_000_000

ARGUMENT_REGISTERS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)


class LinkedCodec:
    """ARMv6-M calls into the exact codec linked into the audited image."""

    def __init__(self, elf_path: Path) -> None:
        self.emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.emulator.mem_map(FLASH_BASE, FLASH_SIZE)
        self.emulator.mem_map(RAM_BASE, RAM_SIZE)
        self.emulator.mem_map(SCRATCH_BASE, SCRATCH_SIZE)

        with elf_path.open("rb") as elf_file:
            elf = ELFFile(elf_file)
            for segment in elf.iter_segments():
                if segment["p_type"] != "PT_LOAD":
                    continue
                virtual_address = int(segment["p_vaddr"])
                file_bytes = segment.data()
                self.emulator.mem_write(virtual_address, file_bytes)

            symbol_table = elf.get_section_by_name(".symtab")
            if symbol_table is None:
                raise RuntimeError("kernel ELF has no symbol table")
            self.symbols = {
                symbol.name: int(symbol["st_value"])
                for symbol in symbol_table.iter_symbols()
                if symbol.name
            }

        required_symbols = (
            "heatshrink_encoder_reset",
            "heatshrink_encoder_sink",
            "heatshrink_encoder_poll",
            "heatshrink_encoder_finish",
            "heatshrink_decoder_reset",
            "heatshrink_decoder_sink",
            "heatshrink_decoder_poll",
            "heatshrink_decoder_finish",
        )
        missing_symbols = [name for name in required_symbols if name not in self.symbols]
        if missing_symbols:
            raise RuntimeError(f"missing linked symbols: {missing_symbols}")

    def call(self, symbol_name: str, *arguments: int) -> int:
        if len(arguments) > len(ARGUMENT_REGISTERS):
            raise ValueError("AAPCS register-call harness accepts at most four arguments")
        for register in ARGUMENT_REGISTERS:
            self.emulator.reg_write(register, 0)
        active_registers = ARGUMENT_REGISTERS[: len(arguments)]
        for register, argument in zip(active_registers, arguments, strict=True):
            self.emulator.reg_write(register, argument & 0xFFFFFFFF)
        self.emulator.reg_write(UC_ARM_REG_SP, STACK_TOP)
        self.emulator.reg_write(UC_ARM_REG_LR, RETURN_ADDRESS | 1)
        entry_address = self.symbols[symbol_name]
        self.emulator.emu_start(
            entry_address | 1,
            RETURN_ADDRESS,
            count=MAX_CALL_INSTRUCTIONS,
        )
        program_counter = self.emulator.reg_read(UC_ARM_REG_PC)
        if program_counter != RETURN_ADDRESS:
            raise RuntimeError(
                f"{symbol_name} stopped at 0x{program_counter:08x}, "
                f"expected 0x{RETURN_ADDRESS:08x}"
            )
        return self.emulator.reg_read(UC_ARM_REG_R0) & 0xFFFFFFFF

    def write_count(self, value: int = 0) -> None:
        self.emulator.mem_write(COUNT_ADDRESS, struct.pack("<I", value))

    def read_count(self) -> int:
        return struct.unpack("<I", self.emulator.mem_read(COUNT_ADDRESS, 4))[0]

    def encode(self, source: bytes) -> bytes:
        if len(source) > TRANSFER_BUFFER_SIZE:
            raise ValueError("source exceeds emulated transfer buffer")
        self.emulator.mem_write(INPUT_ADDRESS, source)
        self.call("heatshrink_encoder_reset", ENCODER_ADDRESS)

        input_offset = 0
        encoded = bytearray()
        while input_offset < len(source):
            self.write_count()
            result = self.call(
                "heatshrink_encoder_sink",
                ENCODER_ADDRESS,
                INPUT_ADDRESS + input_offset,
                len(source) - input_offset,
                COUNT_ADDRESS,
            )
            consumed = self.read_count()
            if result != 0 or consumed == 0:
                raise RuntimeError(
                    f"encoder sink result={result:#x}, consumed={consumed}"
                )
            input_offset += consumed
            self._poll_encoder(encoded)

        while True:
            finish_result = self.call("heatshrink_encoder_finish", ENCODER_ADDRESS)
            if finish_result == 0:
                break
            if finish_result != 1:
                raise RuntimeError(f"encoder finish result={finish_result:#x}")
            produced = self._poll_encoder(encoded)
            if produced == 0:
                raise RuntimeError("encoder requested finish polling without output")
        return bytes(encoded)

    def _poll_encoder(self, destination: bytearray) -> int:
        total_produced = 0
        while True:
            self.write_count()
            result = self.call(
                "heatshrink_encoder_poll",
                ENCODER_ADDRESS,
                OUTPUT_ADDRESS,
                TRANSFER_BUFFER_SIZE,
                COUNT_ADDRESS,
            )
            produced = self.read_count()
            if result not in (0, 1):
                raise RuntimeError(f"encoder poll result={result:#x}")
            if produced:
                destination.extend(self.emulator.mem_read(OUTPUT_ADDRESS, produced))
                total_produced += produced
            if result == 0:
                return total_produced
            if produced == 0:
                raise RuntimeError("encoder reported more output without producing bytes")

    def decode(self, encoded: bytes) -> bytes:
        if len(encoded) > TRANSFER_BUFFER_SIZE:
            raise ValueError("encoded stream exceeds emulated transfer buffer")
        self.emulator.mem_write(INPUT_ADDRESS, encoded)
        self.call("heatshrink_decoder_reset", DECODER_ADDRESS)

        input_offset = 0
        decoded = bytearray()
        while input_offset < len(encoded):
            self.write_count()
            result = self.call(
                "heatshrink_decoder_sink",
                DECODER_ADDRESS,
                INPUT_ADDRESS + input_offset,
                len(encoded) - input_offset,
                COUNT_ADDRESS,
            )
            consumed = self.read_count()
            if result not in (0, 1) or consumed == 0:
                raise RuntimeError(
                    f"decoder sink result={result:#x}, consumed={consumed}"
                )
            input_offset += consumed
            self._poll_decoder(decoded)

        while True:
            finish_result = self.call("heatshrink_decoder_finish", DECODER_ADDRESS)
            if finish_result == 0:
                break
            if finish_result != 1:
                raise RuntimeError(f"decoder finish result={finish_result:#x}")
            produced = self._poll_decoder(decoded)
            if produced == 0:
                raise RuntimeError("decoder requested finish polling without output")
        return bytes(decoded)

    def _poll_decoder(self, destination: bytearray) -> int:
        total_produced = 0
        while True:
            self.write_count()
            result = self.call(
                "heatshrink_decoder_poll",
                DECODER_ADDRESS,
                OUTPUT_ADDRESS,
                TRANSFER_BUFFER_SIZE,
                COUNT_ADDRESS,
            )
            produced = self.read_count()
            if result not in (0, 1):
                raise RuntimeError(f"decoder poll result={result:#x}")
            if produced:
                destination.extend(self.emulator.mem_read(OUTPUT_ADDRESS, produced))
                total_produced += produced
            if result == 0:
                return total_produced
            if produced == 0:
                raise RuntimeError("decoder reported more output without producing bytes")


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def corruption_observations(codec: LinkedCodec, encoded: bytes, expected: bytes) -> dict:
    observations: dict[str, dict[str, object]] = {}
    variants: dict[str, bytes] = {"truncate_last_byte": encoded[:-1]}
    for label, byte_index in (
        ("flip_first_bit", 0),
        ("flip_middle_bit", len(encoded) // 2),
        ("flip_last_bit", len(encoded) - 1),
    ):
        mutated = bytearray(encoded)
        mutated[byte_index] ^= 0x01
        variants[label] = bytes(mutated)

    for label, variant in variants.items():
        try:
            decoded = codec.decode(variant)
            observations[label] = {
                "api_error": False,
                "decoded_bytes": len(decoded),
                "matches": decoded == expected,
                "sha256": digest(decoded),
            }
        except RuntimeError as error:
            observations[label] = {"api_error": True, "error": str(error)}
    return observations


def main() -> None:
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument(
        "--tree-root",
        type=Path,
        default=DEFAULT_TREE_ROOT,
        help="repository tree root (default: two levels above this script)",
    )
    argument_parser.add_argument(
        "--kernel-elf",
        type=Path,
        default=None,
        help="kernel ELF path (default: TREE_ROOT/sys/arch/rp2040/compile/PICO/unix)",
    )
    argument_parser.add_argument(
        "--page-directory",
        type=Path,
        default=None,
        help="man1 page directory (default: TREE_ROOT/share/man/man1)",
    )
    arguments = argument_parser.parse_args()

    tree_root = arguments.tree_root.resolve()
    kernel_elf = arguments.kernel_elf or (
        tree_root / "sys/arch/rp2040/compile/PICO/unix"
    )
    page_directory = arguments.page_directory or (tree_root / "share/man/man1")

    codec = LinkedCodec(kernel_elf)
    pages = {
        name: (page_directory / f"{name}.0").read_bytes() for name in PAGE_NAMES
    }

    encoded_pages: dict[str, bytes] = {}
    for name, source in pages.items():
        encoded = codec.encode(source)
        decoded = codec.decode(encoded)
        if decoded != source:
            raise RuntimeError(f"round trip mismatch for {name}")
        encoded_pages[name] = encoded
        print(
            json.dumps(
                {
                    "kind": "page",
                    "name": name,
                    "raw_bytes": len(source),
                    "encoded_bytes": len(encoded),
                    "ratio": len(source) / len(encoded),
                    "raw_sha256": digest(source),
                    "encoded_sha256": digest(encoded),
                    "roundtrip": True,
                    "corruption": corruption_observations(codec, encoded, source),
                },
                sort_keys=True,
            )
        )

    combined_source = b"".join(pages.values())
    combined_encoded = codec.encode(combined_source)
    if codec.decode(combined_encoded) != combined_source:
        raise RuntimeError("combined stream round trip mismatch")

    print(
        json.dumps(
            {
                "kind": "totals",
                "raw_bytes": len(combined_source),
                "individual_encoded_bytes": sum(map(len, encoded_pages.values())),
                "combined_encoded_bytes": len(combined_encoded),
                "individual_ratio": len(combined_source)
                / sum(map(len, encoded_pages.values())),
                "combined_ratio": len(combined_source) / len(combined_encoded),
                "combined_encoded_sha256": digest(combined_encoded),
                "combined_roundtrip": True,
                "combined_corruption": corruption_observations(
                    codec, combined_encoded, combined_source
                ),
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
