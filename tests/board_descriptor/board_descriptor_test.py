"""Calibrate the board descriptor reader and UF2 patcher."""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    """Apply one source mutant and reject an ambiguous mutation anchor."""
    count = text.count(old)
    if count != 1:
        raise AssertionError(f"{label} anchor occurs {count} times")
    return text.replace(old, new)


def compile_reader(cc: str, top: Path, source: Path, output: Path) -> None:
    """Compile the production reader and its host fixture with strict warnings."""
    subprocess.run(
        [
            cc,
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            "-DBOARD_DESC_HOST",
            "-idirafter",
            str(top / "sys/arch/rp2040/compile/PICO"),
            str(source),
            str(top / "tests/board_descriptor/board_descriptor_test.c"),
            "-o",
            str(output),
        ],
        check=True,
    )


def run_reader(cc: str, top: Path, source: Path, output: Path) -> int:
    compile_reader(cc, top, source, output)
    return subprocess.run([str(output)], check=False).returncode


def load_patcher(path: Path):
    spec = importlib.util.spec_from_file_location("board_descriptor_patcher", path)
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load mkboarduf2.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_round_trip(patcher, kernel: Path) -> None:
    """Require one changed block and only descriptor bytes within that block."""
    original = kernel.read_bytes()
    patched = patcher.patch_image(original, "PICOCALC")
    if len(original) != len(patched):
        raise AssertionError("patch changed UF2 length")
    changed = [index for index, pair in enumerate(zip(original, patched)) if pair[0] != pair[1]]
    if not changed:
        raise AssertionError("PicoCalc patch changed no bytes")
    changed_blocks = {offset // patcher.UF2_BLOCK_SIZE for offset in changed}
    if len(changed_blocks) != 1:
        raise AssertionError(f"patch changed {len(changed_blocks)} UF2 blocks")
    payload_offset = (
        next(iter(changed_blocks)) * patcher.UF2_BLOCK_SIZE + patcher.UF2_PAYLOAD_OFFSET
    )
    descriptor_range = range(payload_offset, payload_offset + patcher.BOARD_DESC.size)
    if any(offset not in descriptor_range for offset in changed):
        raise AssertionError("patch changed a byte outside the descriptor")
    if patched[payload_offset : payload_offset + patcher.BOARD_DESC.size] != patcher.descriptor(
        "PICOCALC"
    ):
        raise AssertionError("patched descriptor bytes are incorrect")
    print(f"board descriptor UF2: one block changed at index {next(iter(changed_blocks))}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    parser.add_argument("--top", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    arguments = parser.parse_args()
    top = arguments.top.resolve()
    board_source = top / "sys/arch/rp2040/rp2040/board.c"
    patcher_source = top / "distrib/rp2040/mkboarduf2.py"

    with tempfile.TemporaryDirectory() as temporary_name:
        temporary = Path(temporary_name)
        if run_reader(arguments.cc, top, board_source, temporary / "reader") != 0:
            raise AssertionError("production board reader rejected its fixtures")

        source_text = board_source.read_text()
        reader_mutants = (
            (
                "skip CRC",
                "if (board_crc32((const volatile unsigned char *)descriptor, checksum_length) !=\n"
                "\t    descriptor->crc32)",
                "if (board_crc32((const volatile unsigned char *)descriptor, checksum_length) ==\n"
                "\t    descriptor->crc32 && 0)",
            ),
            (
                "accept unknown board",
                "descriptor->board != BOARD_ID_PICOCALC) ||",
                "0) ||",
            ),
        )
        for index, (label, old, new) in enumerate(reader_mutants):
            mutant_source = temporary / f"reader-mutant-{index}.c"
            mutant_source.write_text(replace_once(source_text, old, new, label))
            status = run_reader(
                arguments.cc, top, mutant_source, temporary / f"reader-mutant-{index}"
            )
            if status != 1:
                raise AssertionError(f"{label} mutant exited {status}, expected 1")
            print(f"board descriptor mutant rejected: {label}")

        patcher = load_patcher(patcher_source)
        patcher.self_test()
        check_round_trip(patcher, arguments.kernel)

        patcher_text = patcher_source.read_text()
        patcher_text = replace_once(
            patcher_text,
            "if len(offsets) != 1:",
            "if len(offsets) == 0:",
            "patch a second block count",
        )
        patcher_text = replace_once(
            patcher_text,
            "offset = offsets[0]\n"
            "    patched[offset : offset + BOARD_DESC.size] = descriptor(board)",
            "for offset in offsets:\n"
            "        patched[offset : offset + BOARD_DESC.size] = descriptor(board)",
            "patch a second block loop",
        )
        mutant_patcher = temporary / "mkboarduf2-second-block.py"
        mutant_patcher.write_text(patcher_text)
        status = subprocess.run(
            [sys.executable, str(mutant_patcher), "--self-test"], check=False
        ).returncode
        if status == 0:
            raise AssertionError("patch a second block mutant escaped")
        print("board descriptor mutant rejected: patch a second block")

    print("board descriptor gate: production passed and 3 mutants rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
