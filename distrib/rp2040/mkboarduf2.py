"""Patch the RP2040 board descriptor carried by one kernel image block.

A UF2 image carries the descriptor at the start of one 256-byte payload; a
flat unix.bin carries it at the start of one 256-byte block, because
conf/kern.ldscript aligns the .board section to the UF2 payload size.
"""

from __future__ import annotations

import argparse
import struct
import tempfile
import zlib
from pathlib import Path

UF2_BLOCK_SIZE = 512
UF2_PAYLOAD_OFFSET = 32
UF2_HEADER = struct.Struct("<8I")
UF2_MAGIC = (0x0A324655, 0x9E5D5157)
UF2_END_MAGIC = 0x0AB16F30
UF2_FAMILY_FLAG = 0x00002000
RP2040_FAMILY = 0xE48BFF56
FLAT_BLOCK_SIZE = 256

BOARD_DESC_MAGIC = 0x44524F42
BOARD_DESC_VERSION = 1
BOARD_DESC = struct.Struct("<IHHBBBBI")
BOARD_FIELDS = {
    "PICO": (1, 7, 0),
    "PICO_UART": (1, 6, 0),
    "PICOCALC": (2, 10, 0),
    "PICOCALC_PSRAM": (2, 10, 0),
}


def descriptor(board: str) -> bytes:
    """Return the complete little-endian descriptor for a named board."""
    board_id, console_major, console_minor = BOARD_FIELDS[board]
    prefix = struct.pack(
        "<IHHBBBB",
        BOARD_DESC_MAGIC,
        BOARD_DESC_VERSION,
        BOARD_DESC.size,
        board_id,
        console_major,
        console_minor,
        0,
    )
    return prefix + struct.pack("<I", zlib.crc32(prefix))


def is_descriptor(image: bytes, offset: int) -> bool:
    """True when a whole, self-consistent descriptor starts at offset.

    The magic word alone also appears in kernel text: the descriptor
    reader in sys/arch/rp2040/rp2040/board.c loads BOARD_DESC_MAGIC from a
    literal pool, and a pool word that lands at the start of a block reads
    as a second descriptor. A descriptor also carries its version, its
    length and the CRC-32 of the fields before the CRC, which code bytes
    after a pool word do not reproduce.
    """
    if offset + BOARD_DESC.size > len(image):
        return False
    magic, version, length, *_, crc = BOARD_DESC.unpack_from(image, offset)
    prefix = image[offset : offset + BOARD_DESC.size - 4]
    return (
        magic == BOARD_DESC_MAGIC
        and version == BOARD_DESC_VERSION
        and length == BOARD_DESC.size
        and crc == zlib.crc32(prefix)
    )


def payload_offsets(image: bytes) -> list[int]:
    """Return UF2 payload offsets where a whole descriptor starts."""
    if not image or len(image) % UF2_BLOCK_SIZE:
        raise ValueError("UF2 size is not a positive multiple of 512")
    offsets = []
    block_count = len(image) // UF2_BLOCK_SIZE
    for block_index in range(block_count):
        block_offset = block_index * UF2_BLOCK_SIZE
        header = UF2_HEADER.unpack_from(image, block_offset)
        end_magic = struct.unpack_from("<I", image, block_offset + 508)[0]
        if header[:2] != UF2_MAGIC or end_magic != UF2_END_MAGIC:
            raise ValueError(f"block {block_index}: bad UF2 magic")
        flags, payload_size, family = header[2], header[4], header[7]
        if not flags & UF2_FAMILY_FLAG or family != RP2040_FAMILY:
            raise ValueError(f"block {block_index}: family is not RP2040")
        if payload_size != 256:
            raise ValueError(f"block {block_index}: payload size is not 256")
        payload_offset = block_offset + UF2_PAYLOAD_OFFSET
        if is_descriptor(image, payload_offset):
            offsets.append(payload_offset)
    return offsets


def flat_offsets(image: bytes) -> list[int]:
    """Return flat-image block offsets where a whole descriptor starts."""
    if not image:
        raise ValueError("flat image is empty")
    return [
        offset
        for offset in range(0, len(image) - BOARD_DESC.size + 1, FLAT_BLOCK_SIZE)
        if is_descriptor(image, offset)
    ]


def patch_image(image: bytes, board: str, flat: bool = False) -> bytes:
    """Patch exactly one descriptor and preserve every other image byte."""
    offsets = flat_offsets(image) if flat else payload_offsets(image)
    if len(offsets) != 1:
        raise ValueError(f"found {len(offsets)} board descriptor blocks, expected one")
    patched = bytearray(image)
    offset = offsets[0]
    patched[offset : offset + BOARD_DESC.size] = descriptor(board)
    return bytes(patched)


def make_block(payload: bytes, address: int, number: int, total: int) -> bytes:
    """Build one valid synthetic RP2040 UF2 block for the self-test."""
    block = bytearray(UF2_BLOCK_SIZE)
    UF2_HEADER.pack_into(
        block,
        0,
        *UF2_MAGIC,
        UF2_FAMILY_FLAG,
        address,
        256,
        number,
        total,
        RP2040_FAMILY,
    )
    block[UF2_PAYLOAD_OFFSET : UF2_PAYLOAD_OFFSET + len(payload)] = payload
    struct.pack_into("<I", block, 508, UF2_END_MAGIC)
    return bytes(block)


def self_test() -> None:
    """Exercise success, preservation, missing, and duplicate detection."""
    original_descriptor = descriptor("PICO")
    payloads = [bytes([0x11]) * 256, original_descriptor + bytes([0x22]) * 240]
    image = b"".join(
        make_block(payload, 0x10000000 + index * 256, index, len(payloads))
        for index, payload in enumerate(payloads)
    )
    patched = patch_image(image, "PICOCALC")
    descriptor_offset = UF2_BLOCK_SIZE + UF2_PAYLOAD_OFFSET
    expected = bytearray(image)
    expected[descriptor_offset : descriptor_offset + BOARD_DESC.size] = descriptor("PICOCALC")
    if patched != bytes(expected):
        raise AssertionError("patch changed bytes outside the descriptor")
    for label, candidate in (
        ("missing", image[:UF2_BLOCK_SIZE]),
        ("duplicate", image + image[UF2_BLOCK_SIZE:]),
    ):
        try:
            patch_image(candidate, "PICO")
        except ValueError:
            continue
        raise AssertionError(f"accepted a {label} descriptor")
    # A flat image holds the magic a second time inside a block, as the
    # literal pool of the kernel's reader does; only a block start counts.
    literal = bytes([0x33]) * 4 + original_descriptor[:4] + bytes([0x33]) * 248
    flat = payloads[0] + literal + payloads[1]
    flat_offset = 2 * FLAT_BLOCK_SIZE
    flat_expected = bytearray(flat)
    flat_expected[flat_offset : flat_offset + BOARD_DESC.size] = descriptor("PICO_UART")
    if patch_image(flat, "PICO_UART", flat=True) != bytes(flat_expected):
        raise AssertionError("flat patch changed bytes outside the descriptor")
    # The literal can also land at a block start, where only the rest of
    # the descriptor tells it apart.
    aligned = original_descriptor[:4] + bytes([0x33]) * 252
    aligned_flat = payloads[0] + aligned + payloads[1]
    aligned_offset = 2 * FLAT_BLOCK_SIZE
    aligned_expected = bytearray(aligned_flat)
    aligned_expected[aligned_offset : aligned_offset + BOARD_DESC.size] = descriptor("PICO_UART")
    if patch_image(aligned_flat, "PICO_UART", flat=True) != bytes(aligned_expected):
        raise AssertionError("an aligned magic literal counted as a descriptor")
    for label, candidate in (
        ("missing", flat[:flat_offset]),
        ("duplicate", flat + payloads[1]),
        ("empty", b""),
    ):
        try:
            patch_image(candidate, "PICO", flat=True)
        except ValueError:
            continue
        raise AssertionError(f"accepted a flat image with a {label} descriptor")
    print("mkboarduf2: self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--board", choices=sorted(BOARD_FIELDS))
    parser.add_argument(
        "--flat", action="store_true", help="SOURCE is a flat unix.bin rather than a UF2"
    )
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    arguments = parser.parse_args()
    if arguments.self_test:
        if arguments.board or arguments.flat or arguments.source or arguments.output:
            parser.error("--self-test takes no other arguments")
        self_test()
        return
    if arguments.board is None or arguments.source is None or arguments.output is None:
        parser.error("--board BOARD SOURCE OUTPUT are required")
    try:
        patched = patch_image(arguments.source.read_bytes(), arguments.board, arguments.flat)
    except (OSError, ValueError) as error:
        raise SystemExit(f"mkboarduf2: {error}") from None
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=arguments.output.parent, delete=False) as temporary:
        temporary.write(patched)
        temporary_path = Path(temporary.name)
    temporary_path.replace(arguments.output)


if __name__ == "__main__":
    main()
