"""Check linked RP2040 exception-vector slots against their named symbols.

Entries 12 and 13 are the image identity words of tools/kernsum: the length
of the flash image and its CRC32. The word after the 48-entry table,
0x100001C0, addresses the board descriptor. Entries 4 to 10 hold zero: the
PicoCalc UF2 loader writes its stamp over bytes 0x110-0x12B after it
flashes an application, and replaces bytes 0-255 with its own second stage.
The CRC reads those spans, the CRC word and the board descriptor as zero.
The check rebuilds the flash image from the PT_LOAD segments and recomputes
the sum, then requires its own comparison to reject that image with one
covered byte changed and with each word changed, and to accept it with a
loader stamp, a replaced second stage or a rewritten descriptor, so a
comparison that accepts everything or rejects the loader's writes fails
here.
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
BOOT2_SIZE = 0x100
# The loader's stamp: vector entries 4 to 10, flash bytes 0x110-0x12B.
STAMP_START = 0x110
STAMP_END = 0x12C
LENGTH = object()
CRC = object()
SLOTS = (
    "_estack", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
    None, None, None, None, None, None, None, "SVC_Handler", LENGTH, CRC,
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
# The word after the table holds the address of the board descriptor.
BOARD_WORD = VECTOR_ADDRESS - IMAGE_ADDRESS + 4 * len(SLOTS)
LENGTH_WORD = VECTOR_ADDRESS - IMAGE_ADDRESS + 4 * SLOTS.index(LENGTH)
CRC_WORD = VECTOR_ADDRESS - IMAGE_ADDRESS + 4 * SLOTS.index(CRC)


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
    check_board_word(elf, data, table)
    text_start = symbol(table, "Reset_Handler", "STT_FUNC") & ~1
    check_identity(elf, text_start - IMAGE_ADDRESS)


def check_board_word(elf, data, table):
    """Require the word after the table to address the board descriptor."""
    word = symbol(table, "g_boardAddress", "STT_OBJECT", 4)
    if word != VECTOR_ADDRESS + 4 * len(SLOTS):
        raise ValueError("board address word does not follow the vector table")
    value = struct.unpack_from("<I", data, offset_for(elf, data, word, 4))[0]
    if value != symbol(table, "__board_start", "STT_NOTYPE"):
        raise ValueError("board address word differs from __board_start")


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


def identity_spans(image, forget=()):
    """Return the (start, end) spans of image that the CRC reads as zero.

    forget names spans to leave out ("boot2", "stamp" or "board"): a
    comparison built that way is a mutant, and the fixture requires it to
    reject an image that the loader rewrote.
    """
    board = struct.unpack_from("<I", image, BOARD_WORD)[0] - IMAGE_ADDRESS
    spans = {
        "boot2": (0, BOOT2_SIZE),
        "stamp": (STAMP_START, STAMP_END),
        "crc": (CRC_WORD, CRC_WORD + 4),
        "board": (board, board + BOARD_DESCRIPTOR_SIZE),
    }
    return [span for name, span in spans.items() if name not in forget]


def identity_sum(image, forget=()):
    """Return the CRC32 of image with its excluded spans read as zero."""
    summed = bytearray(image)
    for start, end in identity_spans(image, forget):
        summed[start:end] = bytes(end - start)
    return zlib.crc32(summed)


def identity_error(image, forget=()):
    """Return why the identity words of a flash image are wrong, or None."""
    if len(image) < BOARD_WORD + 4 + BOARD_DESCRIPTOR_SIZE:
        return "flash image ends before the identity words"
    length, crc = struct.unpack_from("<2I", image, LENGTH_WORD)
    board = struct.unpack_from("<I", image, BOARD_WORD)[0] - IMAGE_ADDRESS
    if length != len(image):
        return f"length word is {length}, flash image is {len(image)} bytes"
    if board < BOARD_WORD + 4 or board + BOARD_DESCRIPTOR_SIZE > length or board & 255:
        return "board descriptor address lies outside the flash image"
    summed = identity_sum(image, forget)
    if crc != summed:
        return f"CRC word is {crc:08x}, flash image sums to {summed:08x}"
    return None


def check_identity(elf, text_start):
    image = flash_image(elf)
    error = identity_error(image)
    if error is not None:
        raise ValueError(error)
    board = struct.unpack_from("<I", image, BOARD_WORD)[0] - IMAGE_ADDRESS
    # Covered bytes: any change must be caught.
    rejected = {
        "a changed last byte": (len(image) - 1, 1),
        "a changed first vector byte": (BOOT2_SIZE, 1),
        "a changed byte before the loader's stamp": (STAMP_START - 1, 1),
        "a changed byte after the loader's stamp": (STAMP_END, 1),
        "a changed length word": (LENGTH_WORD, 4),
        "a changed CRC word": (CRC_WORD, 4),
        "a changed board address word": (BOARD_WORD, 4),
        "a changed first text byte": (text_start, 1),
    }
    for label, (offset, size) in rejected.items():
        mutant = bytearray(image)
        for position in range(offset, offset + size):
            mutant[position] ^= 0xFF
        if identity_error(mutant) is None:
            raise ValueError(f"identity check accepts {label}")
    # Excluded bytes: a rewrite must be accepted, so that a board-specific
    # copy of the image or an image the loader flashed keeps the identity of
    # the build.
    accepted = {
        "a rewritten board descriptor": (board + 8, 1),
        "a changed first byte": (0, 1),
        "a changed last second-stage byte": (BOOT2_SIZE - 1, 1),
        "a changed first stamp byte": (STAMP_START, 1),
        "a changed last stamp byte": (STAMP_END - 1, 1),
    }
    for label, (offset, size) in accepted.items():
        patched = bytearray(image)
        for position in range(offset, offset + size):
            patched[position] ^= 0xFF
        if identity_error(patched) is not None:
            raise ValueError(f"identity check rejects {label}")


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
