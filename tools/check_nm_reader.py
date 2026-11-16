"""Exercise native nm archive boundaries with independently encoded objects."""

import argparse
import pathlib
import re
import shlex
import struct
import subprocess
import tempfile


def object_bytes(name=b"alpha", value=0xF1234567):
    """Encode the port's eight-word header and compact symbol record."""
    symbol = bytes((len(name), 0x21)) + struct.pack("<I", value) + name
    return struct.pack("<8I", 0o406, 0, 0, 0, 0, 0, len(symbol), 0) + symbol


def header(name, size):
    fields = f"{name:<16}{0:<12}{0:<6}{0:<6}{0o644:<8o}{size:<10}`\n"
    encoded = fields.encode("ascii")
    if len(encoded) != 60:
        raise ValueError("archive header exceeds its field widths")
    return encoded


def member(name, payload, extended=False):
    prefix = name.encode("ascii") if extended else b""
    field = f"#1/{len(prefix)}" if extended else name
    stored = prefix + payload
    return header(field, len(stored)) + stored + (b"\n" if len(stored) & 1 else b"")


def fixtures():
    magic = b"!<arch>\n"
    tail = member("tail.o", object_bytes(b"tail", 2))
    for name, extended in (("short.o", False), ("x" * 17, True), ("y" * 18, True)):
        for symbol in (b"alpha", b"alpha2"):
            for indexed in (False, True):
                index = member("__.SYMDEF", b"\0\0\0\0") if indexed else b""
                data = magic + index + member(name, object_bytes(symbol), extended) + tail
                yield (
                    f"valid-{name}-{len(symbol)}-{indexed}",
                    data,
                    True,
                    [(name, f"f1234567 A {symbol.decode()}"), ("tail.o", "00000002 A tail")],
                )
    for object_magic in (0o406, 0o407, 0o410):
        symbol = object_bytes(b"offset_probe", 0x12345678)[32:]
        sections = b"T" * 3 + b"D" * 5
        if object_magic == 0o406:
            sections += b"R" * 7 + b"S" * 9
        payload = struct.pack("<8I", object_magic, 3, 5, 0, 7, 9, len(symbol), 0)
        payload += sections + symbol
        yield (
            f"section-offset-{object_magic:o}",
            magic + member("offset.o", payload),
            True,
            [("offset.o", "12345678 A offset_probe")],
        )
    yield "empty", magic, True, []
    yield (
        "nonobject",
        magic + member("notes", b"plain text") + tail,
        True,
        [("tail.o", "00000002 A tail")],
    )
    payload = object_bytes()
    for padding in range(1, 5):
        padded = bytearray(payload + bytes(padding))
        padded[24:28] = struct.pack("<I", len(padded) - 32)
        yield (
            f"symbol-padding-{padding}",
            magic + member("object.o", padded),
            True,
            [("object.o", "f1234567 A alpha")],
        )
    for length in (4, 8, 16, 31):
        yield f"short-object-{length}", magic + member("object.o", payload[:length]), False, None
        yield f"standalone-short-{length}", payload[:length], False, None
    yield (
        "index-magic",
        magic + member("__.SYMDEF", payload[:16]) + tail,
        True,
        [("tail.o", "00000002 A tail")],
    )
    valid = magic + member("object.o", payload)
    nul_size = bytearray(valid)
    nul_size[8 + 48 : 8 + 58] = b"43\0garbage"
    yield "nul-size", bytes(nul_size), False, None
    yield "partial-header", valid + b"short", False, None
    yield (
        "bad-trailer",
        magic + header("object.o", len(payload))[:-2] + b"!!" + payload,
        False,
        None,
    )
    yield "missing-padding", valid[:-1], False, None
    yield "partial-body", valid[:-7], False, None
    yield "name-exceeds-size", magic + header("#1/17", 3) + b"abc\n", False, None
    yield "zero-name", magic + header("#1/0", len(payload)) + payload + b"\n", False, None
    yield "invalid-size", magic + header("object.o", "x") + payload, False, None
    yield "negative-size", magic + header("object.o", -1) + payload, False, None
    yield "partial-symbol", magic + member("object.o", payload[:-1]), False, None
    oversized = bytearray(payload)
    oversized[24:28] = struct.pack("<I", 0xFFFFFFFF)
    yield "oversized-symbols", magic + member("object.o", oversized), False, None
    oversized = bytearray(payload)
    oversized[4:8] = struct.pack("<I", 0xFFFFFFFF)
    yield "oversized-text", magic + member("object.o", oversized), False, None
    zero_name = bytearray(payload)
    zero_name[32] = 0
    yield "zero-symbol-name", magic + member("object.o", zero_name), False, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--allocation-binary", type=pathlib.Path)
    parser.add_argument("--io-binary", type=pathlib.Path)
    parser.add_argument("--runner", default="")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    failures = 0
    total = 0
    with tempfile.TemporaryDirectory(prefix="nm-reader-") as directory:
        records = [
            ("zulu", 0x21, 0xF1234567),
            ("local", 0x01, 3),
            ("undefined", 0x20, 0),
            ("common", 0x20, 5),
            ("alpha", 0x21, 3),
        ]
        symbols = b"".join(
            bytes((len(name), kind)) + struct.pack("<I", value) + name.encode()
            for name, kind, value in records
        )
        path = pathlib.Path(directory) / "options.o"
        path.write_bytes(struct.pack("<8I", 0o406, 0, 0, 0, 0, 0, len(symbols), 0) + symbols)
        for flags in ("", "g", "u", "n", "r", "p", "nr", "gnr", "gu", "o", "uo"):
            selected = [record for record in records if "g" not in flags or record[1] & 0x20]
            if "p" not in flags:
                selected.sort(
                    key=lambda row: (row[2], row[0]) if "n" in flags else row[0],
                    reverse="r" in flags,
                )
            expected_lines = []
            for name, kind, value in selected:
                if "u" in flags and (kind != 0x20 or value != 0):
                    continue
                prefix = str(path) + ":" if "o" in flags else ""
                if "u" in flags:
                    line = name
                else:
                    letter = "a" if kind == 1 else "A" if kind == 0x21 else "C" if value else "U"
                    number = f"{value:08x}" if letter != "U" else " " * 8
                    line = f"{number} {letter} {name}"
                expected_lines.append(prefix + line)
            result = subprocess.run(
                [
                    *shlex.split(args.runner),
                    str(binary),
                    *(["-" + flags] if flags else []),
                    str(path),
                ],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
            )
            total += 1
            if result.returncode or result.stderr or result.stdout.splitlines() != expected_lines:
                failures += 1
                print(
                    f"FAIL options {flags!r}: status={result.returncode}, output={result.stdout!r}"
                )
        if args.io_binary:
            io_binary = args.io_binary.resolve(strict=True)
            path = pathlib.Path(directory) / "io.a"
            payload = bytearray(object_bytes())
            payload.extend(b"\0")
            payload[24:28] = struct.pack("<I", len(payload) - 32)
            path.write_bytes(b"!<arch>\n" + member("extended-member.o", payload, True))
            control = subprocess.run(
                [str(io_binary), "0", str(path)],
                capture_output=True,
                timeout=5,
                check=False,
            )
            match = re.search(rb"io-control calls=(\d+) outstanding=0 status=0", control.stderr)
            if control.returncode != 0 or match is None:
                raise RuntimeError("I/O control fails before fault injection")
            total += 1
            for fail_at in range(1, int(match[1]) + 1):
                total += 1
                result = subprocess.run(
                    [str(io_binary), str(fail_at), str(path)],
                    capture_output=True,
                    timeout=5,
                    check=False,
                )
                if result.returncode != 0 or b"nm:" not in result.stderr:
                    failures += 1
                    print(f"FAIL io {fail_at}: {result.stderr!r}")
        if args.allocation_binary:
            allocation_binary = args.allocation_binary.resolve(strict=True)
            path = pathlib.Path(directory) / "allocation.o"
            first = object_bytes(b"first")
            second = object_bytes(b"second")
            combined = bytearray(first + second[32:])
            combined[24:28] = struct.pack("<I", len(combined) - 32)
            path.write_bytes(combined)
            for fail_at in range(4):
                total += 1
                result = subprocess.run(
                    [str(allocation_binary), str(fail_at), str(path)],
                    capture_output=True,
                    timeout=5,
                    check=False,
                )
                if result.returncode != 0:
                    failures += 1
                    print(f"FAIL allocation {fail_at}: {result.stderr!r}")
        for label, data, valid, expected in fixtures():
            path = pathlib.Path(directory) / (label + ".a")
            path.write_bytes(data)
            for flags in ([], ["-g"], ["-o"], ["-go"]):
                total += 1
                result = subprocess.run(
                    [*shlex.split(args.runner), str(binary), *flags, str(path)],
                    capture_output=True,
                    text=True,
                    errors="backslashreplace",
                    timeout=5,
                    check=False,
                )
                lines = [
                    line for line in result.stdout.splitlines() if line and not line.endswith(":")
                ]
                expected_lines = None
                if valid:
                    expected_lines = [
                        f"{path}:{member_name}:{text}" if flags and "o" in flags[0] else text
                        for member_name, text in expected
                    ]
                passed = (
                    result.returncode == 0 and not result.stderr and lines == expected_lines
                    if valid
                    else result.returncode > 0 and bool(result.stderr)
                )
                if not passed:
                    failures += 1
                    print(
                        f"FAIL {label} {flags}: status={result.returncode}, "
                        f"stderr={result.stderr!r}"
                    )
    print(f"native nm reader: {total - failures}/{total} pass")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
