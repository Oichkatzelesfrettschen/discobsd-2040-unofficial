"""Describe successful packed-exec stream work from a validated container."""

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import check_kernel_headroom

EXEC = struct.Struct("<8I")
HSX = struct.Struct("<12I")
HEADER_BYTES = EXEC.size + HSX.size
DEFAULT_LINKER_SCRIPT = Path(__file__).resolve().parent.parent / "sys/arch/rp2040/conf/RP2040.ld"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect(packed, raw, tool, stage_bytes, linker_script=DEFAULT_LINKER_SCRIPT):
    if isinstance(stage_bytes, bool) or not isinstance(stage_bytes, int) or stage_bytes <= 0:
        raise ValueError("stage size must be a positive integer")
    packed_bytes = packed.read_bytes()
    raw_bytes = raw.read_bytes()
    tool_hash = digest(tool.read_bytes())
    if len(packed_bytes) < HEADER_BYTES or len(raw_bytes) < EXEC.size:
        raise ValueError("executable header is truncated")
    packed_exec = EXEC.unpack_from(packed_bytes)
    raw_exec = EXEC.unpack_from(raw_bytes)
    hsx = HSX.unpack_from(packed_bytes, EXEC.size)
    packed_text, packed_data = hsx[6:8]
    script_bytes = linker_script.read_bytes()
    try:
        user_base, user_window = check_kernel_headroom.region_bounds(
            script_bytes.decode("utf-8"), "USERRAM"
        )
    except (UnicodeDecodeError, check_kernel_headroom.InputError) as error:
        raise ValueError(f"invalid user memory region: {error}") from error
    text_size, data_size, bss_size = raw_exec[1:4]
    image_size = text_size + data_size
    entry = raw_exec[7]
    if (raw_exec[0] != 0o407 or image_size > 0xffffffff or
            image_size + bss_size > 0xffffffff or image_size + EXEC.size > 0xffffffff or
            image_size + bss_size > user_window or not (entry & 1) or
            (entry & ~1) < user_base or (entry & ~1) - user_base >= text_size):
        raise ValueError("raw image fails the RP2040 a.out layout checks")
    if len(packed_bytes) != HEADER_BYTES + packed_text + packed_data:
        raise ValueError("packed stream lengths do not match the container")
    if len(raw_bytes) < EXEC.size + raw_exec[1] + raw_exec[2]:
        raise ValueError("raw image is shorter than its text and data")
    if (raw_exec[1:4], raw_exec[7]) != (packed_exec[1:4], packed_exec[7]):
        raise ValueError("packed and raw image layouts differ")
    with tempfile.TemporaryDirectory(prefix="discobsd-packed-geometry-") as temporary:
        directory = Path(temporary)
        saved = directory / "packed"
        decoded = directory / "decoded"
        saved.write_bytes(packed_bytes)
        result = subprocess.run([str(tool), "-d", str(saved), str(decoded)],
                                capture_output=True, text=True, check=False)
        if result.returncode:
            raise ValueError(f"packed container rejected by decoder: {result.stderr.strip()}")
        if digest(tool.read_bytes()) != tool_hash:
            raise ValueError("decoder tool changed during inspection")
        decoded_bytes = decoded.read_bytes()
    expected_body = raw_bytes[EXEC.size:EXEC.size + raw_exec[1] + raw_exec[2]]
    if decoded_bytes[EXEC.size:] != expected_body:
        raise ValueError("packed text and data differ from the supplied raw image")
    fills_text = (packed_text + stage_bytes - 1) // stage_bytes
    fills_data = (packed_data + stage_bytes - 1) // stage_bytes
    return {"schema": 1, "evidence": "successful loader source-path model; not a runtime capture",
            "raw_sha256": digest(raw_bytes), "packed_sha256": digest(packed_bytes),
            "decoder_tool_sha256": tool_hash, "linker_script_sha256": digest(script_bytes),
            "user_base": user_base, "user_window_bytes": user_window,
            "stage_bytes_declared": stage_bytes,
            "raw_file_bytes": len(raw_bytes), "packed_file_bytes": len(packed_bytes),
            "expanded_text_bytes": raw_exec[1], "expanded_data_bytes": raw_exec[2],
            "packed_text_bytes": packed_text, "packed_data_bytes": packed_data,
            "packed_stream_fill_calls_per_pass": fills_text + fills_data,
            "packed_stream_fill_calls_two_passes": 2 * (fills_text + fills_data),
            "packed_stream_input_bytes_two_passes": 2 * (packed_text + packed_data),
            "expanded_crc_bytes_two_passes": 2 * (raw_exec[1] + raw_exec[2]),
            "raw_body_rdwri_calls": 1,
            "exclusions": "header reads and CRC, swap-in, failures, filesystem splitting, "
                          "stack admission, cache and latency"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--stage-bytes", type=int, required=True)
    parser.add_argument("--linker-script", type=Path, default=DEFAULT_LINKER_SCRIPT)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.packed, args.raw, args.decoder, args.stage_bytes,
                                 args.linker_script),
                         indent=2, sort_keys=True))
    except (OSError, ValueError) as error:
        print(f"packed exec work: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
