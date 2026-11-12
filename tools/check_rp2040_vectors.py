"""Check linked RP2040 exception-vector slots against their named symbols.

Entries 8, 9 and 10 are the image identity words of tools/kernsum: the
length of the flash image, its CRC32 with the CRC word and the board
descriptor read as zero, and the descriptor's address. The check rebuilds
the flash image from the PT_LOAD segments and recomputes both, then
requires its own comparison to reject that image with one byte changed and
with each word changed, so a comparison that accepts everything fails here.
"""

import argparse
import io
import struct
import sys
import zlib
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

VECTOR_ADDRESS = 0x10000100
IMAGE_ADDRESS = 0x10000000
BOARD_DESCRIPTOR_SIZE = 16
LENGTH = object()
CRC = object()
SLOTS = (
    "_estack", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
    None, None, None, None, LENGTH, CRC, "__board_start", "SVC_Handler", None, None,
    "PendSV_Handler", "SysTick_Handler",
    "TIMER_IRQ_0_Handler", "TIMER_IRQ_1_Handler", "TIMER_IRQ_2_Handler",
    "TIMER_IRQ_3_Handler", "PWM_IRQ_WRAP_Handler", "USBCTRL_IRQ_Handler",
    "XIP_IRQ_Handler", "PIO0_IRQ_0_Handler", "PIO0_IRQ_1_Handler",
    "PIO1_IRQ_0_Handler", "PIO1_IRQ_1_Handler", "DMA_IRQ_0_Handler",
    "DMA_IRQ_1_Handler", "IO_IRQ_BANK0_Handler", "IO_IRQ_QSPI_Handler",
    "SIO_IRQ_PROC0_Handler", "SIO_IRQ_PROC1_Handler", "CLOCKS_IRQ_Handler",
    "SPI0_IRQ_Handler", "SPI1_IRQ_Handler", "UART0_IRQ_Handler", "UART1_IRQ_Handler",
    "ADC_IRQ_FIFO_Handler", "I2C0_IRQ_Handler", "I2C1_IRQ_Handler", "RTC_IRQ_Handler",
    "SPARE_IRQ_0_Handler", "SPARE_IRQ_1_Handler", "SPARE_IRQ_2_Handler",
    "SPARE_IRQ_3_Handler", "SPARE_IRQ_4_Handler", "SPARE_IRQ_5_Handler",
)


def check(path):
    data = path.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    if (elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM" or
            elf.header.e_type != "ET_EXEC"):
        raise ValueError("expected a linked little-endian ARM32 ELF")
    table = elf.get_section_by_name(".symtab")
    if table is None or table.header.sh_type != "SHT_SYMTAB":
        raise ValueError("symbol table is absent")
    address = symbol(table, "g_pfnVectors", "STT_OBJECT", len(SLOTS) * 4)
    if address != VECTOR_ADDRESS:
        raise ValueError("vector table address differs from the boot2 entry")
    offset = offset_for(elf, data, address, len(SLOTS) * 4)
    words = struct.unpack_from(f"<{len(SLOTS)}I", data, offset)
    for index, (name, value) in enumerate(zip(SLOTS, words)):
        if name is None:
            if value != 0:
                raise ValueError(f"reserved vector {index} is nonzero")
            continue
        if name is LENGTH or name is CRC:
            continue
        if name == "__board_start":
            if value != symbol(table, name, "STT_NOTYPE"):
                raise ValueError(f"vector {index} differs from {name}")
            continue
        kind = "STT_NOTYPE" if index == 0 else "STT_FUNC"
        expected = symbol(table, name, kind)
        if value != expected:
            raise ValueError(f"vector {index} differs from {name}")
        if index == 0:
            if value & 7:
                raise ValueError("initial stack pointer is not eight-byte aligned")
        else:
            if not value & 1:
                raise ValueError(f"vector {index} lacks the Thumb bit")
            offset_for(elf, data, value & ~1, 2, executable=True)
    check_identity(elf)


def flash_image(elf):
    """Return the bytes the PT_LOAD segments place from IMAGE_ADDRESS up."""
    image = bytearray()
    for segment in elf.iter_segments():
        header = segment.header
        if header.p_type != "PT_LOAD" or header.p_filesz == 0:
            continue
        start = header.p_paddr - IMAGE_ADDRESS
        if start < 0:
            raise ValueError("a segment loads below the flash image")
        end = start + header.p_filesz
        image.extend(bytes(max(0, end - len(image))))
        image[start:end] = segment.data()
    return image


def identity_error(image):
    """Return why the identity words of a flash image are wrong, or None."""
    words = VECTOR_ADDRESS - IMAGE_ADDRESS + 4 * SLOTS.index(LENGTH)
    if len(image) < words + 12:
        return "flash image ends before the identity words"
    length, crc, board = struct.unpack_from("<3I", image, words)
    board -= IMAGE_ADDRESS
    if length != len(image):
        return f"length word is {length}, flash image is {len(image)} bytes"
    if board < words + 12 or board + BOARD_DESCRIPTOR_SIZE > length:
        return "board descriptor address lies outside the flash image"
    summed = bytearray(image)
    summed[words + 4:words + 8] = bytes(4)
    summed[board:board + BOARD_DESCRIPTOR_SIZE] = bytes(BOARD_DESCRIPTOR_SIZE)
    if crc != zlib.crc32(summed):
        return f"CRC word is {crc:08x}, flash image sums to {zlib.crc32(summed):08x}"
    return None


def check_identity(elf):
    image = flash_image(elf)
    error = identity_error(image)
    if error is not None:
        raise ValueError(error)
    words = VECTOR_ADDRESS - IMAGE_ADDRESS + 4 * SLOTS.index(LENGTH)
    board = struct.unpack_from("<I", image, words + 8)[0] - IMAGE_ADDRESS
    mutants = {
        "a changed last byte": (len(image) - 1, 1),
        "a changed first byte": (0, 1),
        "a changed length word": (words, 4),
        "a changed CRC word": (words + 4, 4),
    }
    for label, (offset, size) in mutants.items():
        mutant = bytearray(image)
        for position in range(offset, offset + size):
            mutant[position] ^= 0xFF
        if identity_error(mutant) is None:
            raise ValueError(f"identity check accepts {label}")
    # The descriptor is outside the sum by design: a board-specific copy of
    # the image keeps the identity of the build.
    patched = bytearray(image)
    patched[board + 8] ^= 0xFF
    if identity_error(patched) is not None:
        raise ValueError("identity check rejects a rewritten board descriptor")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernels", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        for kernel in args.kernels:
            check(kernel)
            print(f"PASS RP2040 vectors: {kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 vectors: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
