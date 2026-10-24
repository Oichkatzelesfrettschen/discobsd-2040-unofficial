"""Calibrate shell command, getopts, and execution-input ILP32 witnesses."""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SHELL_SOURCE = ROOT / "bin/sh"
TEST_DIRECTORY = SHELL_SOURCE / "tests"
BASELINE_OBJECTS = TEST_DIRECTORY / "out"
FIXTURES = (
    TEST_DIRECTORY / "fixtures/command-null-builtin.json",
    TEST_DIRECTORY / "fixtures/getopts-optind-skip.json",
    TEST_DIRECTORY / "fixtures/execution-input-eval-mode.json",
    TEST_DIRECTORY / "fixtures/execution-input-argument-cursor.json",
    TEST_DIRECTORY / "fixtures/execution-input-descriptor-mode.json",
)
COMPILE_FLAGS = (
    "-m32",
    "-std=gnu89",
    "-O1",
    "-w",
    "-fcommon",
    "-include",
    str(TEST_DIRECTORY / "hostshim.h"),
    "-Dalloc=sh_alloc",
    "-Dfree=sh_free",
    "-Dtmpnam=sh_tmpnam",
    "-I",
    str(SHELL_SOURCE),
)
SHELL_ENVIRONMENT_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class WitnessError(RuntimeError):
    """Report a witness or calibration contradiction."""


def require(condition, message):
    if not condition:
        raise WitnessError(message)


def load_fixture(path):
    fixture = json.loads(path.read_text(encoding="utf-8"))
    require(fixture.get("schema_version") == 1, f"bad fixture schema: {path}")
    require(
        set(fixture) == {
            "schema_version",
            "id",
            "source",
            "object",
            "old",
            "new",
            "snippet",
            "environment",
            "known_good",
            "known_bad",
            "caught_break",
        },
        f"malformed fixture: {path}",
    )
    for state in ("known_good", "known_bad"):
        require(
            set(fixture[state]) == {"stdout", "exit_code"},
            f"malformed {state}: {path}",
        )
    require(isinstance(fixture["environment"], dict), f"bad environment: {path}")
    require(
        all(
            isinstance(name, str) and isinstance(value, str)
            for name, value in fixture["environment"].items()
        ),
        f"non-string environment entry: {path}",
    )
    require(fixture["old"] != fixture["new"], f"inert mutation: {path}")
    require(bool(fixture["caught_break"]), f"unnamed break: {path}")
    return fixture


def shell_environment(extra_environment):
    # A directly executed bmake recipe can inherit dotted internal controls.
    # The witness owns command/getopts behavior, so give the subject the same
    # identifier-shaped environment that an intervening POSIX shell exports.
    candidate_environment = os.environ.copy()
    candidate_environment.update(extra_environment)
    return {
        name: value
        for name, value in candidate_environment.items()
        if SHELL_ENVIRONMENT_NAME.fullmatch(name)
    }


def run_shell(executable, snippet, work_directory, extra_environment):
    result = subprocess.run(
        [os.fspath(executable), "-c", snippet],
        cwd=work_directory,
        env=shell_environment(extra_environment),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=10,
    )
    return {"stdout": result.stdout.decode("utf-8", "replace"), "exit_code": result.returncode}


def compile_mutant(fixture, compiler, temporary_root):
    temporary_root.mkdir()
    source_path = ROOT / fixture["source"]
    source = source_path.read_text(encoding="utf-8")
    require(source.count(fixture["old"]) == 1, f"mutation anchor drift: {fixture['id']}")
    mutant_source = temporary_root / source_path.name
    mutant_source.write_text(source.replace(fixture["old"], fixture["new"]), encoding="utf-8")

    object_directory = temporary_root / "objects"
    shutil.copytree(BASELINE_OBJECTS, object_directory)
    mutant_object = object_directory / fixture["object"]
    subprocess.run(
        [*compiler, *COMPILE_FLAGS, "-c", "-o", os.fspath(mutant_object), os.fspath(mutant_source)],
        check=True,
        timeout=30,
    )
    executable = temporary_root / f"sh-{fixture['id']}"
    objects = sorted(object_directory.glob("*.o"))
    require(bool(objects), "baseline shell objects are absent")
    subprocess.run(
        [*compiler, "-m32", "-o", os.fspath(executable), *map(os.fspath, objects)],
        check=True,
        timeout=30,
    )
    return executable


def main():
    baseline = BASELINE_OBJECTS / "sh"
    require(baseline.is_file(), "run bin/sh/tests/posix-sh.sh before calibration")
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    require(bool(compiler), "HOST_CC selects no compiler")

    with tempfile.TemporaryDirectory(prefix="discobsd-shell-witness-") as temporary:
        temporary_root = Path(temporary)
        work_directory = temporary_root / "work"
        work_directory.mkdir()
        for fixture_path in FIXTURES:
            fixture = load_fixture(fixture_path)
            observed_good = run_shell(
                baseline,
                fixture["snippet"],
                work_directory,
                fixture["environment"],
            )
            require(
                observed_good == fixture["known_good"],
                f"known-good witness drift: {fixture['id']}: {observed_good}",
            )
            mutant = compile_mutant(fixture, compiler, temporary_root / fixture["id"])
            observed_bad = run_shell(
                mutant,
                fixture["snippet"],
                work_directory,
                fixture["environment"],
            )
            require(
                observed_bad == fixture["known_bad"],
                f"known-bad witness drift: {fixture['id']}: {observed_bad}",
            )
            require(
                observed_bad != observed_good,
                f"mutation escaped witness: {fixture['id']}",
            )
            print(f"PASS shell witness mutation: {fixture['id']} caught {fixture['caught_break']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, WitnessError, subprocess.SubprocessError) as error:
        print(f"FAIL shell witness calibration: {error}", file=sys.stderr)
        sys.exit(1)
