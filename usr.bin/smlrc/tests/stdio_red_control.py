#!/usr/bin/env python3
"""Prove that the Smaller C stdio consumer reaches libc's refill member."""

import hashlib
import os
import shlex
import shutil
import subprocess
import tempfile
from collections import Counter
from pathlib import Path


def run(command, *, cwd=None, env=None, text=True):
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        capture_output=True,
        text=text,
    )


def require_success(result, operation):
    if result.returncode == 0:
        return
    output = result.stdout + result.stderr
    raise RuntimeError(f"{operation} failed with status {result.returncode}:\n{output}")


def make_words(root, machine, directory, variable):
    result = run(
        [
            "bmake",
            f"MACHINE={machine}",
            "-C",
            str(root / directory),
            "-V",
            "${" + variable + "}",
        ]
    )
    require_success(result, f"evaluate {directory} {variable}")
    return shlex.split(result.stdout.strip())


def archive_members(archiver, archive):
    result = run([*archiver, "t", str(archive)])
    require_success(result, f"list {archive}")
    return result.stdout.splitlines()


def archive_member(archiver, archive, member):
    result = run([*archiver, "p", str(archive), member], text=False)
    if result.returncode != 0:
        diagnostic = (result.stdout + result.stderr).decode(errors="replace")
        raise RuntimeError(f"read {member} from {archive}:\n{diagnostic}")
    return result.stdout


def digest(content):
    return hashlib.sha256(content).hexdigest()


def harness_environment(root, machine, cpu, library, output):
    environment = os.environ.copy()
    environment.update(
        {
            "TOPSRC": str(root),
            "MACHINE": machine,
            "CPU": cpu,
            "TOOLBINDIR": os.environ.get(
                "TOOLBINDIR", str(root / "tools" / "bin" / machine)
            ),
            "PYTHON": os.environ["PYTHON"],
            "HOST_CC": os.environ.get("HOST_CC", "cc"),
            "DEVICE_CC": os.environ["DEVICE_CC"],
            "DEVICE_LDFLAGS": os.environ["DEVICE_LDFLAGS"],
            "REQUIRE_QEMU": "1",
            "SMLRC_TEST_LIBC": str(library),
            "SMLRC_TEST_ONLY": "t18_stdio",
            "SMLRC_TEST_OUT": str(output),
        }
    )
    return environment


def run_stdio_harness(root, environment):
    return run(
        [str(root / "usr.bin" / "smlrc" / "tests" / "run.sh")],
        cwd=root / "usr.bin" / "smlrc",
        env=environment,
    )


def main():
    required_environment = ("TOPSRC", "PYTHON", "DEVICE_CC", "DEVICE_LDFLAGS")
    missing = [name for name in required_environment if not os.environ.get(name)]
    if missing:
        raise RuntimeError("missing environment: " + ", ".join(missing))

    if shutil.which("qemu-arm") is None:
        if os.environ.get("REQUIRE_QEMU") == "1":
            raise RuntimeError("qemu-arm is required for the stdio red control")
        print("SKIP Smaller C stdio red control: qemu-arm is unavailable")
        return 0

    root = Path(os.environ["TOPSRC"]).resolve()
    machine = os.environ.get("MACHINE", "rp2040")
    cpu = os.environ.get("CPU", "cortex-m0plus")
    expected_architectures = {
        "cortex-m0plus": ("v6-M", "v6S-M"),
        "cortex-m4": ("v7E-M",),
    }.get(cpu)
    if expected_architectures is None:
        raise RuntimeError(f"unsupported CPU for the stdio red control: {cpu}")
    source_library = root / "lib" / "libc.a"
    source_file = root / "lib" / "libc" / "stdio" / "filbuf.c"
    if not source_library.is_file():
        raise RuntimeError(f"missing source archive: {source_library}")

    compiler = make_words(root, machine, Path("lib/libc/stdio"), "CC")
    compiler_flags = make_words(root, machine, Path("lib/libc/stdio"), "CFLAGS")
    archiver = make_words(root, machine, Path("lib/libc/stdio"), "AR")
    ranlib = make_words(root, machine, Path("lib/libc/stdio"), "RANLIB")

    with tempfile.TemporaryDirectory(prefix="discobsd-smlrc-stdio-") as temporary:
        work = Path(temporary)
        mutant_source = work / "filbuf.c"
        mutant_object = work / "filbuf.o"
        mutant_library = work / "libc-mutant.a"

        source_text = source_file.read_text(encoding="utf-8")
        original_return = "return (*iop->_ptr++ & 0377);"
        mutant_return = "return ((*iop->_ptr++ & 0377) ^ 1);"
        if source_text.count(original_return) != 1:
            raise RuntimeError("filbuf success return is not the calibrated source shape")
        mutant_source.write_text(
            source_text.replace(original_return, mutant_return), encoding="utf-8"
        )

        compile_result = run(
            [
                *compiler,
                *compiler_flags,
                "-c",
                str(mutant_source),
                "-o",
                str(mutant_object),
            ]
        )
        require_success(compile_result, "compile damaged filbuf.o")

        readelf = run(
            [
                os.environ.get("CROSS", "arm-none-eabi") + "-readelf",
                "-A",
                str(mutant_object),
            ]
        )
        require_success(readelf, "read damaged filbuf.o attributes")
        architecture = next(
            (
                line.split(":", 1)[1].strip()
                for line in readelf.stdout.splitlines()
                if "Tag_CPU_arch:" in line
            ),
            "",
        )
        if architecture not in expected_architectures:
            raise RuntimeError(
                "damaged filbuf.o lacks the expected "
                f"{cpu} architecture attribute: {architecture or 'absent'}"
            )

        original_members = archive_members(archiver, source_library)
        if original_members.count("filbuf.o") != 1:
            raise RuntimeError("source archive does not contain exactly one filbuf.o")
        original_member = archive_member(archiver, source_library, "filbuf.o")

        shutil.copy2(source_library, mutant_library)
        replace_result = run(
            [*archiver, "r", str(mutant_library), str(mutant_object)]
        )
        require_success(replace_result, "replace filbuf.o in damaged archive")
        ranlib_result = run([*ranlib, str(mutant_library)])
        require_success(ranlib_result, "index damaged archive")

        mutant_members = archive_members(archiver, mutant_library)
        if Counter(mutant_members) != Counter(original_members):
            raise RuntimeError("damaged archive changed the member inventory")
        mutant_member = archive_member(archiver, mutant_library, "filbuf.o")
        if original_member == mutant_member:
            raise RuntimeError("damaged archive retained the original filbuf.o bytes")

        red_environment = harness_environment(
            root, machine, cpu, mutant_library, work / "red-run"
        )
        red_result = run_stdio_harness(root, red_environment)
        red_output = red_result.stdout + red_result.stderr
        expected_rejection = "FAIL t18_stdio: exited 10 under qemu"
        if red_result.returncode == 0 or expected_rejection not in red_output:
            raise RuntimeError(
                "damaged filbuf.o did not reach the expected t18_stdio assertion:\n"
                + red_output
            )
        for suffix in (".elf", ".aout", ".qemu.elf"):
            artifact = work / "red-run" / ("t18_stdio" + suffix)
            if not artifact.is_file():
                raise RuntimeError(f"red control lacks linked artifact: {artifact}")

        green_environment = harness_environment(
            root, machine, cpu, source_library, work / "green-run"
        )
        green_result = run_stdio_harness(root, green_environment)
        green_output = green_result.stdout + green_result.stderr
        require_success(green_result, "restore original filbuf.o")
        if "PASS t18_stdio" not in green_output or "passed 1, failed 0" not in green_output:
            raise RuntimeError("restored stdio control lacks the green receipt")

        map_text = (work / "green-run" / "t18_stdio.map").read_text(
            encoding="utf-8", errors="replace"
        )
        for member in ("filbuf.o", "ungetc.o", "scanf.o", "doscan.o", "closedir.o",
                       "fprintf.o", "vfprintf.o", "fputs.o"):
            if member not in map_text:
                raise RuntimeError(f"t18_stdio link map omits {member}")

        print(
            "Smaller C stdio red control: filbuf.o "
            f"{digest(original_member)[:16]} -> {digest(mutant_member)[:16]}; "
            "damaged archive rejected; original archive restored green"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
