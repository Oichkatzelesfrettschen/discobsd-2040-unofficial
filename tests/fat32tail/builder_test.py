"""Tests for tools/picocalc/mkloaderuf2.py, the loader composite builder.

usage: builder_test.py TOPSRC WORKDIR

Known-good inputs build the layout the locator's header defines; each rule
the builder enforces has a known-bad input that it must refuse with a
message and no output file. WORKDIR receives the files the command-line
cases write; the test removes each by name.
"""

from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

TOPSRC = Path(sys.argv[1])
WORK = Path(sys.argv[2])
sys.path.insert(0, str(TOPSRC / "tools" / "picocalc"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixtures  # noqa: E402
import mkloaderuf2 as builder  # noqa: E402

SCRIPT = TOPSRC / "tools" / "picocalc" / "mkloaderuf2.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"builder_test: FAIL: {message}")


def root_image(blocks_kib: int = 512) -> bytes:
    """A stand-in root image: both magic words, the claimed size, then data."""
    size = blocks_kib * 1024
    data = bytearray(size)
    data[:4] = b"FS<<"
    struct.pack_into("<2I", data, 4, 2, blocks_kib)
    data[1020:1024] = b">>FS"
    for i in range(1024, size, 997):
        data[i] = i & 0xFF
    return bytes(data)


def refused(kernel: bytes, root: bytes, text: str) -> None:
    try:
        builder.build(kernel, root)
    except builder.BuildError as error:
        require(text in str(error), f"refusal says {error!s}, expected {text!r}")
        return
    raise SystemExit(f"builder_test: FAIL: accepted an input that {text}")


def patch_block(kernel: bytes, index: int, offset: int, value: int, width: int = 4) -> bytes:
    data = bytearray(kernel)
    at = index * 512 + offset
    data[at : at + width] = value.to_bytes(width, "little")
    return bytes(data)


def layout() -> None:
    root = root_image()
    for blocks, head in (
        (1, 32768),
        (63, 32768),
        (64, 32768),
        (65, 65536),
        (471, 262144),
        (512, 262144),
        (513, 294912),
    ):
        require(builder.head_size(blocks) == head, f"head_size({blocks}) is not {head}")
        kernel = fixtures.make_uf2(blocks)
        out = builder.build(kernel, root)
        require(len(out) == head + len(root), f"{blocks} blocks: output length {len(out)}")
        require(out[: len(kernel)] == kernel, f"{blocks} blocks: the kernel moved")
        require(
            out[len(kernel) : head] == bytes(head - len(kernel)),
            f"{blocks} blocks: padding is not zero",
        )
        require(out[head:] == root, f"{blocks} blocks: the root image moved")


def kernel_rules() -> None:
    root = root_image()
    good = fixtures.make_uf2(8)
    refused(b"", root, "positive multiple of 512")
    refused(good + b"\0", root, "positive multiple of 512")
    refused(good[:-512] + bytes(512), root, "bad magic")
    refused(patch_block(good, 3, 508, 0), root, "bad magic")
    refused(patch_block(good, 3, 0, 0), root, "bad magic")
    refused(patch_block(good, 2, 8, 0x2001), root, "not-main-flash")
    refused(patch_block(good, 2, 8, 0), root, "family is not RP2040")
    refused(patch_block(good, 2, 28, 0xE48BFF57), root, "family is not RP2040")
    refused(patch_block(good, 2, 16, 512), root, "payload size")
    refused(patch_block(good, 2, 12, 0x10000A80), root, "target alignment")
    refused(patch_block(good, 2, 12, 0x0FFFFF00), root, "below flash")
    refused(patch_block(good, 2, 12, 0x101FC000), root, "loader's own flash")
    refused(patch_block(good, 2, 12, 0x101FBF80 + 0x80), root, "loader's own flash")
    refused(patch_block(good, 4, 20, 5), root, "numbered 5 of 8")
    refused(patch_block(good, 4, 24, 9), root, "numbered 4 of 9")
    refused(patch_block(good, 0, 12, 0x10001000), root, "flash sector 0")
    # The last block may sit just below the loader's flash.
    builder.build(patch_block(good, 7, 12, 0x101FBF00), root)


def root_rules() -> None:
    kernel = fixtures.make_uf2(8)
    good = root_image()
    refused(kernel, good[:1023], "multiple of 512")
    refused(kernel, good[:512], "multiple of 512")
    refused(kernel, bytes(4) + good[4:], "magic words")
    refused(kernel, good[:1020] + bytes(4) + good[1024:], "magic words")
    # The superblock's size and i-list must describe the image exactly.
    refused(kernel, good[:-512], "claims")
    refused(kernel, good[:-1024], "claims")
    refused(kernel, good + bytes(1024), "claims")
    for blocks in (0, 1, 9):
        data = bytearray(good)
        struct.pack_into("<I", data, 8, blocks)
        refused(kernel, bytes(data), "at least 10")
    for isize in (0, 1, 512, 513):
        data = bytearray(good)
        struct.pack_into("<I", data, 4, isize)
        refused(kernel, bytes(data), "i-list size")
    # The smallest volume fsutil creates is accepted.
    builder.build(kernel, root_image(10))


def ram_block(target: int, flags: int = 0x2000, family: int = 0xE48BFF56) -> bytes:
    block = bytearray(512)
    struct.pack_into("<8I", block, 0, 0x0A324655, 0x9E5D5157, flags, target, 256, 3, 9, family)
    struct.pack_into("<I", block, 508, 0x0AB16F30)
    return bytes(block)


def ram_hazard() -> None:
    kernel = fixtures.make_uf2(8)
    root = bytearray(root_image())
    # A block for RAM at a 512-byte-aligned offset starts a RAM application.
    for target in (0x20000000, 0x20041F00):
        data = bytearray(root)
        data[2048 : 2048 + 512] = ram_block(target)
        refused(kernel, bytes(data), "UF2 block for RAM")
    # The same block aimed at flash, for another family, for no flash, past
    # the SRAM range or off its alignment is inert, and a hazard block that
    # starts off a 512-byte boundary is never read as a chunk.
    for block in (
        ram_block(0x10020000),
        ram_block(0x20042000),
        ram_block(0x20000000, family=0xE48BFF59),
        ram_block(0x20000000, flags=0x2001),
        ram_block(0x20000080),
        ram_block(0x1FFFFF00),
    ):
        data = bytearray(root)
        data[2048 : 2048 + 512] = block
        builder.build(kernel, bytes(data))
    data = bytearray(root)
    data[2048 + 256 : 2048 + 768] = ram_block(0x20000000)
    builder.build(kernel, bytes(data))
    # A block with the SRAM target but a block number past its total is ignored.
    bad = bytearray(ram_block(0x20000000))
    struct.pack_into("<I", bad, 20, 9)
    data = bytearray(root)
    data[2048 : 2048 + 512] = bad
    builder.build(kernel, bytes(data))


def names() -> None:
    for text in (
        "discobsd.uf2",
        "DISCOBSD.uf2",
        "a.uf2",
        "12345678.uf2",
        "my-app_1.uf2",
        "x/y/discobsd.uf2",
    ):
        builder.check_output_name(Path(text))
    for text in (
        "discobsd.UF2",
        "discobsd.Uf2",
        "discobsd.bin",
        "discobsd",
        "discobsd.uf",
        ".uf2",
        "123456789.uf2",
        "DiscoBSD.uf2",
        "disco bsd.uf2",
        "a.b.uf2",
        "disco+.uf2",
        "disco.uf2.txt",
        "discobsd.uf2x",
    ):
        try:
            builder.check_output_name(Path(text))
        except builder.BuildError:
            continue
        raise SystemExit(f"builder_test: FAIL: accepted the name {text}")


def command_line() -> None:
    kernel = WORK / "bt-kernel.uf2"
    root = WORK / "bt-root.img"
    out = WORK / "discobsd.uf2"
    made = [kernel, root, out]
    kernel.write_bytes(fixtures.make_uf2(471))
    root.write_bytes(root_image())

    def run(*args: str, cwd: Path = WORK):
        return subprocess.run(
            [sys.executable, str(SCRIPT), *args],
            capture_output=True,
            text=True,
            cwd=cwd,
            check=False,
        )

    try:
        # The default name, in the working directory.
        result = run(str(kernel), str(root))
        require(result.returncode == 0 and result.stderr == "", f"default build: {result.stderr}")
        require(
            out.read_bytes() == builder.build(kernel.read_bytes(), root.read_bytes()),
            "default output",
        )
        out.unlink()
        # An output name that is not discobsd.uf2 builds with a warning.
        other = WORK / "other.uf2"
        made.append(other)
        result = run("-o", str(other), str(kernel), str(root))
        require(result.returncode == 0 and "discobsd.uf2" in result.stderr, "name warning")
        other.unlink()
        # Refusals leave zero output files and say why.
        for bad_out in ("discobsd.UF2", "discobsd.bin", "DiscoBSD.uf2", "toolongname.uf2"):
            path = WORK / bad_out
            made.append(path)
            result = run("-o", str(path), str(kernel), str(root))
            require(
                result.returncode == 1 and result.stderr.startswith("mkloaderuf2:"),
                f"{bad_out}: {result}",
            )
            require(not path.exists(), f"{bad_out} was written")
        short_root = WORK / "bt-short.img"
        made.append(short_root)
        short_root.write_bytes(b"FS<<")
        result = run(str(kernel), str(short_root))
        require(result.returncode == 1 and not out.exists(), "a refused root left an output")
        result = run(str(WORK / "absent.uf2"), str(root))
        require(result.returncode == 1 and not out.exists(), "a missing input left an output")
        # A refused build does not replace an existing output.
        out.write_bytes(b"previous")
        result = run(str(kernel), str(short_root))
        require(
            result.returncode == 1 and out.read_bytes() == b"previous",
            "a refusal replaced the output",
        )
        require(
            not [p for p in WORK.iterdir() if p.name.startswith(".mkloaderuf2.")],
            "temporary file left",
        )
    finally:
        for path in made:
            path.unlink(missing_ok=True)


def main() -> None:
    layout()
    kernel_rules()
    root_rules()
    ram_hazard()
    names()
    command_line()
    print(
        "builder_test: layout, kernel and root rules, RAM-block hazard, names and command line hold"
    )


main()
