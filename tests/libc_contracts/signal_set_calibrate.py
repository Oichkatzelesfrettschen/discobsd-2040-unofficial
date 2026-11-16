"""Require signal-set behavior and named rejection of provider defects."""

import hashlib
import json
import os
import shlex
import subprocess
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / "lib/libc/gen/sigsetops.c").read_text()
    fixture = (ROOT / "tests/libc_contracts/signal_set_contract_test.c").read_text()
    work = ROOT / ".build-logs" / ("signal-set-calibration-" + uuid.uuid4().hex)
    work.mkdir(parents=True)
    controls = [
        ("good", None, None, 0, None),
        (
            "complement-membership",
            "*set & ((sigset_t)1 << (signo - 1))",
            "*set & ~((sigset_t)1 << (signo - 1))",
            1,
            "signal sets: membership",
        ),
        ("empty", "*set = 0;", "*set = 1;", 1, "signal sets: empty"),
        ("full", "*set = ~(sigset_t)0;", "*set = 0;", 1, "signal sets: full"),
        ("add", "*set |=", "*set &=", 1, "signal sets: add singleton"),
        ("delete", "*set &=", "*set |=", 1, "signal sets: delete singleton"),
        ("errno", "errno = EINVAL;", "errno = EDOM;", 3, "signal sets: invalid add"),
        (
            "zero-admission",
            "signo <= 0 || signo >= NSIG",
            "signo < 0 || signo >= NSIG",
            3,
            "runtime error:",
        ),
    ]
    rows = []
    for name, before, after, count, marker in controls:
        content = source
        if before is not None:
            if source.count(before) != count:
                raise RuntimeError("mutation anchor count differs: " + name)
            content = source.replace(before, after)
        provider = work / (name + "-provider.c")
        provider.write_text(content)
        test = work / (name + "-test.c")
        test.write_text(
            fixture.replace('"../../lib/libc/gen/sigsetops.c"', json.dumps(str(provider)))
        )
        binary = work / name
        command = [
            *shlex.split(os.environ.get("CC", "cc")),
            "-std=c17",
            "-pedantic-errors",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
            "-I" + str(ROOT / "tests/libc_contracts/signal_set_include"),
            str(test),
            "-o",
            str(binary),
        ]
        build = subprocess.run(command, capture_output=True, timeout=60, check=False)
        (work / (name + ".build.log")).write_bytes(build.stdout + build.stderr)
        if build.returncode or build.stderr:
            raise RuntimeError("control fails warning-clean compilation: " + name)
        # The sanitizer runtime ends a rejected control by exit status on
        # Linux and by abort() on Darwin unless told otherwise, and an
        # inherited UBSAN_OPTIONS could change either; the control's
        # environment fixes the exit so one expected status holds on both.
        result = subprocess.run(
            [str(binary)],
            capture_output=True,
            timeout=30,
            check=False,
            env={**os.environ, "UBSAN_OPTIONS": "abort_on_error=0:exitcode=1"},
        )
        (work / (name + ".run.log")).write_bytes(result.stdout + result.stderr)
        expected = 1 if marker else 0
        if result.returncode != expected:
            raise RuntimeError(
                "control exit differs: %s: expected %d, got %d"
                % (name, expected, result.returncode)
            )
        if marker and marker.encode() not in result.stderr:
            raise RuntimeError("control misses its named rejection: " + name)
        if not marker and (result.stderr or b"31981/31981 pass" not in result.stdout):
            raise RuntimeError("good control lacks complete clean execution")
        rows.append({"name": name, "command": command, "exit": result.returncode})
        print("PASS signal-set control: " + name, flush=True)
    (work / "receipt.json").write_text(
        json.dumps(
            {
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "fixture_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
                "controls": rows,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
