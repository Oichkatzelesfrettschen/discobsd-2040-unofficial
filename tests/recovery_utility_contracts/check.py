#!/usr/bin/env python3
"""Exercise the DPUCP recovery path and binary-transfer utilities."""

import argparse
import base64
import binascii
import os
import stat
import subprocess
import tempfile
from pathlib import Path


class ContractFailure(RuntimeError):
    """Report one behavioral contract mismatch."""


def compile_program(
    compiler,
    output,
    sources,
    include_directory,
    sanitized=False,
    extra_flags=(),
):
    command = [
        compiler,
        "-std=c17",
        "-D_POSIX_C_SOURCE=200809L",
        "-D_DEFAULT_SOURCE",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-funsigned-char",
    ]
    if sanitized:
        command.extend(
            [
                "-O1",
                "-g",
                "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined",
            ]
        )
    command.extend(extra_flags)
    command.extend(["-I", str(include_directory), "-o", str(output)])
    command.extend(str(source) for source in sources)
    subprocess.run(command, check=True)


def run_program(command, *, input_bytes=None, stdout_path=None):
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
    environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1")
    if stdout_path is None:
        return subprocess.run(
            command,
            input=input_bytes,
            capture_output=True,
            env=environment,
            check=False,
            timeout=10,
        )
    with stdout_path.open("wb") as output_file:
        return subprocess.run(
            command,
            input=input_bytes,
            stdout=output_file,
            stderr=subprocess.PIPE,
            env=environment,
            check=False,
            timeout=10,
        )


def require(condition, description):
    if not condition:
        raise ContractFailure(description)


def traditional_uuencode(payload, output_name="payload.bin", mode=0o640):
    encoded_lines = []
    for offset in range(0, len(payload), 45):
        encoded_lines.append(binascii.b2a_uu(payload[offset : offset + 45]))
    return (
        f"begin {mode:o} {output_name}\n".encode()
        + b"".join(encoded_lines)
        + b"`\nend\n"
    )


def base64_uuencode(payload, output_name="payload.bin", mode=0o640):
    encoded = base64.encodebytes(payload)
    return (
        f"begin-base64 {mode:o} {output_name}\n".encode()
        + encoded
        + b"====\n"
    )


def check_readlink(binary, work_directory):
    target = "target/with spaces"
    link_path = work_directory / "link"
    link_path.symlink_to(target)

    result = run_program([str(binary), str(link_path)])
    require(result.returncode == 0, "readlink rejects a symbolic link")
    require(result.stdout == (target + "\n").encode(), "readlink newline output differs")
    require(result.stderr == b"", "readlink writes a diagnostic on success")

    result = run_program([str(binary), "-n", str(link_path)])
    require(result.returncode == 0, "readlink -n rejects a symbolic link")
    require(result.stdout == target.encode(), "readlink -n emits a trailing newline")

    regular_path = work_directory / "regular"
    regular_path.write_text("data", encoding="ascii")
    result = run_program([str(binary), str(regular_path)])
    require(result.returncode != 0, "readlink accepts a regular file")
    require(result.stdout == b"", "readlink writes stdout for a regular file")
    require(result.stderr, "readlink omits the regular-file diagnostic")

    result = run_program([str(binary), str(work_directory / "missing")])
    require(result.returncode != 0, "readlink accepts a missing path")
    require(result.stderr, "readlink omits the missing-path diagnostic")

    result = run_program([str(binary), str(link_path), str(link_path)])
    require(result.returncode != 0, "readlink accepts more than one operand")

    if Path("/dev/full").exists():
        result = run_program([str(binary), str(link_path)], stdout_path=Path("/dev/full"))
        require(result.returncode != 0, "readlink reports success after a stdout failure")


def check_realpath(binary, work_directory):
    existing_directory = work_directory / "existing"
    existing_directory.mkdir()
    existing_file = existing_directory / "file"
    existing_file.write_text("data", encoding="ascii")
    link_path = work_directory / "file-link"
    link_path.symlink_to(existing_file)

    expected_file = str(existing_file.resolve()).encode() + b"\n"
    result = run_program([str(binary), "-e", str(link_path)])
    require(result.returncode == 0, "realpath -e rejects an existing symlink")
    require(result.stdout == expected_file, "realpath -e resolves an existing symlink incorrectly")
    require(result.stderr == b"", "realpath -e writes a diagnostic on success")

    missing_path = existing_directory / "missing"
    expected_missing = str(missing_path).encode() + b"\n"
    result = run_program([str(binary), "-E", str(missing_path)])
    require(result.returncode == 0, "realpath -E rejects a missing final component")
    require(
        result.stdout == expected_missing,
        "realpath -E canonicalizes a missing final component incorrectly",
    )

    result = run_program([str(binary), "-e", str(missing_path)])
    require(result.returncode != 0, "realpath -e accepts a missing final component")
    require(result.stdout == b"", "realpath -e writes stdout for a failed canonicalization")
    require(result.stderr, "realpath -e omits a failure diagnostic")

    dangling_link = work_directory / "dangling"
    dangling_link.symlink_to("existing/missing")
    result = run_program([str(binary), "-E", str(dangling_link)])
    require(result.returncode == 0, "realpath -E rejects a symlink to a missing final component")
    require(result.stdout == expected_missing, "realpath -E preserves a dangling symlink name")

    missing_parent_link = work_directory / "missing-parent-link"
    missing_parent_link.symlink_to("absent/leaf")
    result = run_program([str(binary), "-E", str(missing_parent_link)])
    require(result.returncode != 0, "realpath -E accepts a missing path prefix")
    require(result.stdout == b"", "realpath -E writes stdout for a missing path prefix")

    result = run_program([str(binary), "-e", "-E", str(missing_path)])
    require(result.returncode == 0, "realpath does not honor the last -E option")
    result = run_program([str(binary), "-E", "-e", str(missing_path)])
    require(result.returncode != 0, "realpath does not honor the last -e option")

    result = run_program([str(binary), str(missing_path)])
    require(result.returncode == 0, "realpath default does not select the documented -E profile")
    require(result.stdout == expected_missing, "realpath default output differs from -E")

    result = run_program([str(binary), str(existing_file), str(existing_file)])
    require(result.returncode != 0, "realpath accepts more than one operand")

    result = run_program([str(binary), "-E", str(missing_path) + "/"])
    require(
        result.returncode == 0,
        "realpath -E rejects trailing slashes on a missing final component",
    )
    require(result.stdout == expected_missing, "realpath -E retains a trailing slash")

    result = run_program([str(binary), "-E", str(existing_file) + "/"])
    require(result.returncode != 0, "realpath -E accepts a trailing slash on a regular file")

    loop_one = work_directory / "loop-one"
    loop_two = work_directory / "loop-two"
    loop_one.symlink_to(loop_two.name)
    loop_two.symlink_to(loop_one.name)
    result = run_program([str(binary), "-E", str(loop_one)])
    require(result.returncode != 0, "realpath -E accepts a symbolic-link loop")

    if Path("/dev/full").exists():
        result = run_program([str(binary), "-e", str(existing_file)], stdout_path=Path("/dev/full"))
        require(result.returncode != 0, "realpath reports success after a stdout failure")


def check_uudecode_native(binary, work_directory):
    payload = bytes(range(256)) + b"\x00binary\xffpayload\n"
    traditional_input = traditional_uuencode(payload)
    result = run_program([str(binary), "-o", "-"], input_bytes=traditional_input)
    require(result.returncode == 0, "uudecode -o - rejects traditional input")
    require(result.stdout == payload, "uudecode -o - changes traditional binary bytes")
    require(result.stderr == b"", "uudecode -o - writes diagnostics on success")

    base64_input = base64_uuencode(payload)
    result = run_program([str(binary), "-o", "/dev/stdout"], input_bytes=base64_input)
    require(result.returncode == 0, "uudecode -o /dev/stdout rejects base64 input")
    require(result.stdout == payload, "uudecode changes base64 binary bytes")
    require(result.stderr == b"", "uudecode base64 writes diagnostics on success")

    output_path = work_directory / "decoded.bin"
    result = run_program([str(binary), "-o", str(output_path)], input_bytes=traditional_input)
    require(result.returncode == 0, "uudecode rejects an output pathname")
    require(output_path.read_bytes() == payload, "uudecode changes file-output bytes")
    require(
        stat.S_IMODE(output_path.stat().st_mode) == 0o640,
        "uudecode does not apply the encoded mode",
    )

    malformed_header = b"begin payload.bin\n"
    result = run_program([str(binary), "-o", "-"], input_bytes=malformed_header)
    require(result.returncode != 0, "uudecode accepts a malformed header")

    truncated_input = b"begin 644 payload.bin\n#04)#\n"
    result = run_program([str(binary), "-o", "-"], input_bytes=truncated_input)
    require(result.returncode != 0, "uudecode accepts input without an end footer")

    if Path("/dev/full").exists():
        result = run_program(
            [str(binary), "-o", "-"],
            input_bytes=traditional_input,
            stdout_path=Path("/dev/full"),
        )
        require(result.returncode != 0, "uudecode reports success after a stdout failure")


def check_uudecode_sanitized(binary):
    malformed_line = b"begin 644 payload.bin\nM!!\n`\nend\n"
    result = run_program([str(binary), "-o", "-"], input_bytes=malformed_line)
    require(result.returncode != 0, "uudecode accepts a short encoded payload line")
    require(
        b"short encoded payload line" in result.stderr,
        "uudecode does not validate the encoded payload length before decoding",
    )
    require(b"AddressSanitizer" not in result.stderr, "uudecode reads beyond a short encoded line")

    high_bit_base64 = b"begin-base64 644 payload.bin\n\xff\n====\n"
    result = run_program([str(binary), "-o", "-"], input_bytes=high_bit_base64)
    require(result.returncode != 0, "uudecode accepts a high-bit base64 byte")
    require(
        b"invalid byte" in result.stderr,
        "uudecode does not classify a high-bit base64 byte as invalid",
    )
    require(
        b"AddressSanitizer" not in result.stderr
        and b"runtime error" not in result.stderr,
        "uudecode indexes the base64 table with a signed byte",
    )


def require_known_bad_rejected(callback, description):
    try:
        callback()
    except ContractFailure:
        return
    raise ContractFailure(f"known-bad fixture passed {description}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--variant", choices=("native", "sanitized"), required=True)
    arguments = parser.parse_args()

    source_root = arguments.source_root.resolve()
    textbox_sources = source_root / "usr.bin" / "textbox"
    with tempfile.TemporaryDirectory(prefix="recovery-utility-contracts-") as temporary:
        build_directory = Path(temporary)
        known_bad = build_directory / "known-bad"
        compile_program(
            arguments.cc,
            known_bad,
            [source_root / "tests" / "recovery_utility_contracts" / "known_bad.c"],
            textbox_sources,
        )
        uudecode_sources = [
            source_root / "usr.bin" / "uudecode" / "uudecode.c",
            textbox_sources / "eprintf.c",
            textbox_sources / "fshut.c",
            textbox_sources / "mode.c",
        ]
        if arguments.variant == "native":
            readlink_binary = build_directory / "readlink"
            realpath_binary = build_directory / "realpath"
            realpath_legacy_guard = build_directory / "realpath-legacy-guard"
            uudecode_binary = build_directory / "uudecode"
            compile_program(
                arguments.cc,
                readlink_binary,
                [source_root / "usr.bin" / "readlink" / "readlink.c"],
                textbox_sources,
            )
            compile_program(
                arguments.cc,
                realpath_binary,
                [source_root / "usr.bin" / "realpath" / "realpath.c"],
                textbox_sources,
            )
            compile_program(
                arguments.cc,
                realpath_legacy_guard,
                [
                    source_root / "usr.bin" / "realpath" / "realpath.c",
                    source_root
                    / "tests"
                    / "recovery_utility_contracts"
                    / "realpath_legacy_guard.c",
                ],
                textbox_sources,
                extra_flags=("-Wl,--wrap=realpath",),
            )
            compile_program(
                arguments.cc,
                uudecode_binary,
                uudecode_sources,
                textbox_sources,
            )
            for name, callback in (
                ("readlink", lambda: check_readlink(known_bad, build_directory / "bad-readlink")),
                ("realpath", lambda: check_realpath(known_bad, build_directory / "bad-realpath")),
                (
                    "uudecode",
                    lambda: check_uudecode_native(
                        known_bad, build_directory / "bad-uudecode"
                    ),
                ),
            ):
                callback_directory = build_directory / f"bad-{name}"
                callback_directory.mkdir()
                require_known_bad_rejected(callback, name)

            work_directory = build_directory / "work"
            work_directory.mkdir()
            check_readlink(readlink_binary, work_directory)
            check_realpath(realpath_binary, work_directory)
            legacy_guard_directory = build_directory / "legacy-realpath-guard"
            legacy_guard_directory.mkdir()
            check_realpath(realpath_legacy_guard, legacy_guard_directory)
            check_uudecode_native(uudecode_binary, work_directory)
            print("recovery utilities: native contracts passed")
        else:
            uudecode_sanitized = build_directory / "uudecode-sanitized"
            compile_program(
                arguments.cc,
                uudecode_sanitized,
                uudecode_sources,
                textbox_sources,
                sanitized=True,
            )
            require_known_bad_rejected(
                lambda: check_uudecode_sanitized(known_bad), "uudecode sanitizer"
            )
            check_uudecode_sanitized(uudecode_sanitized)
            print("recovery utilities: sanitizer contracts passed")


if __name__ == "__main__":
    main()
