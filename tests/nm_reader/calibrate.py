"""Compile native nm controls and require each defect's named rejection."""

import hashlib
import json
import os
import shlex
import subprocess
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    python = os.environ["PYTHON"]
    source = (ROOT / "usr.bin/nm/nm.c").read_text()
    work = ROOT / ".build-logs" / ("nm-reader-calibration-" + uuid.uuid4().hex)
    work.mkdir(parents=True)
    command = [
        *shlex.split(os.environ.get("CC", "cc")),
        "-std=gnu17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DCROSS",
        "-fsanitize=undefined",
        "-fno-sanitize-recover=all",
        "-I" + str(ROOT / "usr.bin/nm"),
        "-I" + str(ROOT / "tools/aoututils/include"),
        "-idirafter",
        str(ROOT / "include"),
    ]
    controls = [
        ("good", None, None, None),
        (
            "unsigned-sort-direction",
            "signed char rflg = 1;",
            "unsigned char rflg = 1;",
            "FAIL options 'r'",
        ),
        (
            "archive-padding",
            "return payload + stored + (stored & 1);",
            "return payload + stored;",
            "FAIL valid-short.o-5-False",
        ),
        (
            "relocation-offset",
            "mag_un.mag_exp.a_reltext : 0;",
            "0 : 0;",
            "FAIL section-offset-406",
        ),
        ("numeric-nul", "end != buf + (len)", "*end != '\\0'", "FAIL nul-size"),
        ("silent-status", "errs = 1;", "errs = 0;", "FAIL partial-header"),
        ("symbol-padding", "if (getc(fi) != 0)", "if (getc(fi) == EOF)", "FAIL zero-symbol-name"),
        (
            "member-prefix",
            'printf("%s:", archive_input ? chdr.name : *xargv);',
            'printf("%s:", *xargv);',
            "FAIL valid-short.o-5-False ['-o']",
        ),
    ]
    rows = []
    for name, before, after, marker in controls:
        content = source
        if before is not None:
            if source.count(before) != 1:
                raise RuntimeError("mutation anchor count differs: " + name)
            content = source.replace(before, after)
        path, binary = work / (name + ".c"), work / name
        path.write_text(content)
        build = subprocess.run(
            [*command, str(path), "-o", str(binary)],
            capture_output=True,
            timeout=60,
            check=False,
        )
        (work / (name + ".build.log")).write_bytes(build.stdout + build.stderr)
        if build.returncode or build.stderr:
            raise RuntimeError("control fails warning-clean compilation: " + name)
        result = subprocess.run(
            [python, str(ROOT / "tools/check_nm_reader.py"), str(binary)],
            capture_output=True,
            timeout=60,
            check=False,
        )
        (work / (name + ".run.log")).write_bytes(result.stdout + result.stderr)
        expected = 0 if marker is None else 1
        if result.returncode != expected or (marker and marker.encode() not in result.stdout):
            raise RuntimeError("control misses its required verdict: " + name)
        rows.append({"name": name, "exit": result.returncode, "marker": marker})
        print("PASS nm reader control: " + name, flush=True)
    allocation = work / "allocation"
    io_binary = work / "io"
    subprocess.run(
        [*command, str(ROOT / "tests/nm_reader/io.c"), "-o", str(io_binary)],
        check=True,
        timeout=60,
    )
    subprocess.run(
        [*command, str(ROOT / "tests/nm_reader/allocation.c"), "-o", str(allocation)],
        check=True,
        timeout=60,
    )
    subprocess.run(
        [
            python,
            str(ROOT / "tools/check_nm_reader.py"),
            str(work / "good"),
            "--allocation-binary",
            str(allocation),
            "--io-binary",
            str(io_binary),
        ],
        check=True,
        timeout=60,
    )
    (work / "receipt.json").write_text(
        json.dumps(
            {
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "controls": rows,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
