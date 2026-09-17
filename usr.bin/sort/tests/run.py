"""Exercise the bounded-memory sort against an independent C-locale oracle."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import signal
import subprocess
import tempfile
import time


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    return parser.parse_args()


def run_command(
    command: list[str],
    *,
    input_bytes: bytes | None = None,
    expected_status: int = 0,
) -> subprocess.CompletedProcess[bytes]:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    result = subprocess.run(
        command,
        input=input_bytes,
        capture_output=True,
        check=False,
        env=environment,
    )
    if result.returncode != expected_status:
        raise SystemExit(
            f"command status {result.returncode}, expected {expected_status}: "
            f"{' '.join(command)}\nstdout:\n{result.stdout!r}\nstderr:\n{result.stderr!r}"
        )
    return result


def require_empty(directory: pathlib.Path, label: str) -> None:
    residue = sorted(path.name for path in directory.iterdir())
    if residue:
        raise SystemExit(f"{label} left temporary files: {residue}")


def compare_with_oracle(
    executable: pathlib.Path,
    oracle: str,
    temporary_directory: pathlib.Path,
    input_bytes: bytes,
    options: list[str],
) -> None:
    run_directory = temporary_directory / f"run-{len(list(temporary_directory.iterdir()))}"
    run_directory.mkdir()
    actual = run_command(
        [str(executable), "-T", str(run_directory), *options],
        input_bytes=input_bytes,
    )
    expected = run_command([oracle, *options], input_bytes=input_bytes)
    if actual.stdout != expected.stdout:
        raise SystemExit(f"sort output differs from the C-locale oracle for {options}")
    require_empty(run_directory, f"sort {options}")


def verify_many_file_merge(
    executable: pathlib.Path,
    oracle: str,
    temporary_directory: pathlib.Path,
) -> None:
    input_directory = temporary_directory / "many-inputs"
    scratch_directory = temporary_directory / "many-scratch"
    input_directory.mkdir()
    scratch_directory.mkdir()
    input_paths = []
    for file_index in range(23):
        lines = [
            f"{(line_index * 37 + file_index * 19) % 997:04d} "
            f"file-{file_index:02d}-line-{line_index:02d}\n".encode("ascii")
            for line_index in range(31)
        ]
        sorted_lines = run_command([oracle], input_bytes=b"".join(lines)).stdout
        input_path = input_directory / f"input-{file_index:02d}"
        input_path.write_bytes(sorted_lines)
        input_paths.append(input_path)
    command_paths = [str(path) for path in input_paths]
    actual = run_command(
        [str(executable), "-T", str(scratch_directory), "-m", *command_paths]
    )
    expected = run_command([oracle, "-m", *command_paths])
    if actual.stdout != expected.stdout:
        raise SystemExit("23-file merge differs from the C-locale oracle")
    require_empty(scratch_directory, "23-file merge")


def verify_output_and_check_modes(
    executable: pathlib.Path,
    temporary_directory: pathlib.Path,
) -> None:
    scratch_directory = temporary_directory / "mode-scratch"
    scratch_directory.mkdir()
    output_path = temporary_directory / "output"
    run_command(
        [str(executable), "-T", str(scratch_directory), "-o", str(output_path)],
        input_bytes=b"z\na\nm\n",
    )
    if output_path.read_bytes() != b"a\nm\nz\n":
        raise SystemExit("-o output differs from the expected order")
    require_empty(scratch_directory, "-o sort")

    run_command(
        [str(executable), "-T", str(scratch_directory), "-c"],
        input_bytes=b"a\nb\nc\n",
    )
    run_command(
        [str(executable), "-T", str(scratch_directory), "-c"],
        input_bytes=b"b\na\n",
        expected_status=1,
    )
    require_empty(scratch_directory, "-c sort")

    merge_output = temporary_directory / "merge-output"
    merge_output.write_bytes(b"a\nc\ne\n")
    run_command(
        [
            str(executable),
            "-T",
            str(scratch_directory),
            "-m",
            "-o",
            str(merge_output),
            str(merge_output),
        ]
    )
    if merge_output.read_bytes() != b"a\nc\ne\n":
        raise SystemExit("-m -o changed an input file before reading it")
    require_empty(scratch_directory, "-m -o sort")


def verify_signal_cleanup(
    executable: pathlib.Path, temporary_directory: pathlib.Path
) -> None:
    scratch_directory = temporary_directory / "signal-scratch"
    scratch_directory.mkdir()
    input_path = temporary_directory / "signal-input"
    input_path.write_bytes(
        b"".join(f"{line_number:08d}\n".encode("ascii") for line_number in range(300000, 0, -1))
    )
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    process = subprocess.Popen(
        [str(executable), "-T", str(scratch_directory), str(input_path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        env=environment,
    )
    deadline = time.monotonic() + 10
    while process.poll() is None and not any(scratch_directory.iterdir()):
        if time.monotonic() >= deadline:
            process.kill()
            process.wait()
            raise SystemExit("sort created no temporary run before the signal deadline")
        time.sleep(0.005)
    if process.poll() is not None:
        raise SystemExit("sort exited before the signal-cleanup probe")
    process.send_signal(signal.SIGTERM)
    _, stderr = process.communicate(timeout=10)
    if process.returncode != 1:
        raise SystemExit(
            f"SIGTERM sort status {process.returncode}, expected 1; stderr={stderr!r}"
        )
    require_empty(scratch_directory, "SIGTERM sort")


def main() -> int:
    arguments = parse_arguments()
    oracle = shutil.which("sort")
    if oracle is None:
        raise SystemExit("a host sort command is required")
    with tempfile.TemporaryDirectory(prefix="discobsd-sort-") as directory_name:
        temporary_directory = pathlib.Path(directory_name)
        executable = temporary_directory / "sort"
        compile_command = [
            arguments.cc,
            "-std=gnu17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-missing-braces",
            "-Wno-parentheses",
            "-Wno-dangling-else",
            "-Wno-unused-parameter",
            "-Wno-implicit-fallthrough",
            # sort.c keeps its K&R definitions, which clang 17 and later
            # reject under -Werror; gcc ignores the unknown -Wno- flag.
            "-Wno-deprecated-non-prototype",
            '-D_PATH_USRTMP="/tmp"',
            "-DSORT_HOST_TEST",
            "-DMEM=16384",
            str(arguments.source),
            "-o",
            str(executable),
        ]
        run_command(compile_command)

        basic_input = b"b-2\nA 10\na-1\nB 2\na-1\n"
        for options in ([], ["-r"], ["-u"], ["-f"], ["-d"], ["-i"], ["-n"]):
            compare_with_oracle(
                executable, oracle, temporary_directory, basic_input, options
            )
        byte_input = b"".join(
            bytes((byte_value,)) + b"x\n"
            for byte_value in range(1, 256)
            if byte_value != ord("\n")
        )
        for options in ([], ["-f"], ["-i"], ["-d"], ["-f", "-i"]):
            compare_with_oracle(
                executable, oracle, temporary_directory, byte_input, options
            )
        external_input = b"".join(
            f"{(line_number * 7919) % 104729:06d} row-{line_number:04d}\n".encode(
                "ascii"
            )
            for line_number in range(4000)
        )
        compare_with_oracle(
            executable, oracle, temporary_directory, external_input, []
        )
        verify_many_file_merge(executable, oracle, temporary_directory)
        verify_output_and_check_modes(executable, temporary_directory)
        verify_signal_cleanup(executable, temporary_directory)

    print("sort: byte domain, external runs, 23-file merge, modes, and cleanup passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
