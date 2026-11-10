"""Calibrate successful packed-exec work geometry against a real packer."""

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import inspect_packed_exec_work as geometry  # noqa: E402


def reject(call, label):
    try:
        call()
    except ValueError:
        return
    raise AssertionError(f"{label} was accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True, type=Path)
    args = parser.parse_args()
    tool = args.tool.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="discobsd-packed-geometry-test-") as temporary:
        directory = Path(temporary)
        raw, packed = directory / "raw", directory / "packed"
        text = bytes((index * 7) & 255 for index in range(1024))
        data = bytes((index * 11) & 255 for index in range(177))
        raw.write_bytes(struct.pack("<8I", 0o407, len(text), len(data), 0, 0, 0, 0,
                                    0x20000001) + text + data)
        subprocess.run([str(tool), "-c", str(raw), str(packed)], check=True)
        result = geometry.inspect(packed, raw, tool, 17)
        expected_text = (result["packed_text_bytes"] + 16) // 17
        expected_data = (result["packed_data_bytes"] + 16) // 17
        fills_per_pass = expected_text + expected_data
        if (result["packed_stream_fill_calls_per_pass"] != fills_per_pass or
                result["packed_stream_fill_calls_two_passes"] != 2 * fills_per_pass or
                result["expanded_crc_bytes_two_passes"] != 2 * (len(text) + len(data)) or
                result["raw_body_rdwri_calls"] != 1):
            raise AssertionError("packed-exec work geometry differs from the source contract")
        reject(lambda: geometry.inspect(packed, raw, tool, 0), "zero stage size")
        original = packed.read_bytes()
        packed.write_bytes(original[:-1])
        reject(lambda: geometry.inspect(packed, raw, tool, 17), "truncated container")
        packed.write_bytes(original[:32] + bytes([original[32] ^ 1]) + original[33:])
        reject(lambda: geometry.inspect(packed, raw, tool, 17), "corrupt packed header")
        packed.write_bytes(original)
        raw_original = raw.read_bytes()
        bad_magic = bytearray(raw_original)
        bad_magic[0] ^= 1
        raw.write_bytes(bad_magic)
        reject(lambda: geometry.inspect(packed, raw, tool, 17), "invalid raw a.out magic")
        raw.write_bytes(raw_original)
        invalid_entry = bytearray(raw_original)
        struct.pack_into("<I", invalid_entry, 28, 0x20000000)
        raw.write_bytes(invalid_entry)
        subprocess.run([str(tool), "-c", str(raw), str(packed)], check=True)
        reject(lambda: geometry.inspect(packed, raw, tool, 17), "non-Thumb entry")
        oversized = bytearray(raw_original)
        struct.pack_into("<I", oversized, 12, 192 * 1024)
        raw.write_bytes(oversized)
        subprocess.run([str(tool), "-c", str(raw), str(packed)], check=True)
        reject(lambda: geometry.inspect(packed, raw, tool, 17), "oversized image")
        packed.write_bytes(original)
        raw.write_bytes(raw_original)
        modified = bytearray(raw.read_bytes())
        modified[-1] ^= 1
        raw.write_bytes(modified)
        reject(lambda: geometry.inspect(packed, raw, tool, 17), "different raw content")
    print("PASS packed-exec-geometry: real container, chunk boundaries and negative controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
