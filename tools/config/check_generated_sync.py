"""Compare production RP2040 Makefiles with isolated real config output."""

import argparse
import difflib
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARDS = ("PICO", "PICO_UART")
COMPILE = Path("sys/arch/rp2040/compile")
CONF = Path("sys/arch/rp2040/conf")
BUILD_INPUTS = tuple(Path("tools/config") / name for name in (
    "Makefile", "config.h", "config.y", "lang.l", "main.c", "mkioconf.c",
    "mkmakefile.c", "mkswapconf.c",
)) + tuple(Path(name) for name in (
    "tools/Makefile.inc", "tools/check-build-machine.sh",
    "share/mk/architecture.mk", "share/mk/warnings.mk",
))
GENERATION_INPUTS = tuple(CONF / name for name in (
    "Makefile.rp2040", "files.rp2040", "devices.rp2040",
)) + tuple(COMPILE / board / "Config" for board in BOARDS)


class SyncError(Exception):
    """A missing input, failed generator or byte mismatch invalidates the gate."""


def copy_file(root, destination, relative):
    source = root / relative
    if not source.is_file():
        raise SyncError(f"missing required input: {relative}")
    target = destination / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def copy_inputs(root, destination, expected=False):
    for relative in BUILD_INPUTS + GENERATION_INPUTS:
        copy_file(root, destination, relative)
    for board in BOARDS:
        board_list = COMPILE / board / ("files." + board)
        if (root / board_list).exists():
            copy_file(root, destination, board_list)
        config = (destination / COMPILE / board / "Config").read_text()
        if not re.search(rf'^board\s+"{board}"\s*(?:#.*)?$', config, re.MULTILINE):
            raise SyncError(f"{board}: Config must retain the production board identity")
        if expected:
            copy_file(root, destination, COMPILE / board / "Makefile")


def child_environment():
    environment = os.environ.copy()
    # Python closes bmake's jobserver descriptors. The private generator build
    # owns its serial scheduling while separate gate invocations remain isolated.
    for variable in ("MAKEFLAGS", "MFLAGS"):
        environment.pop(variable, None)
    return environment


def command_run(command, directory, description):
    try:
        result = subprocess.run(command, cwd=directory, env=child_environment(),
                                capture_output=True, text=True, check=False)
    except OSError as error:
        raise SyncError(f"{description}: {error}") from error
    if result.returncode:
        raise SyncError(f"{description}: exit {result.returncode}\n"
                        f"{result.stdout}{result.stderr}")
    if re.search(r"\bwarning:", result.stdout + result.stderr, re.IGNORECASE):
        raise SyncError(f"{description}: warning diagnostic\n{result.stdout}{result.stderr}")
    return result


def build_generator(workspace, make):
    # lang.l includes y.tab.h; a serial private build respects the generator's
    # parser/header creation order without sharing products with other gates.
    command_run([make, "-j1", "MACHINE=rp2040", "config"], workspace / "tools/config",
                "configuration generator build")
    generator = workspace / "tools/config/config"
    if not generator.is_file():
        raise SyncError("configuration generator build produced no config executable")
    return generator


def generate(generator, directory, board):
    result = command_run([str(generator), "Config"], directory, f"{board}: config generator")
    if result.stderr:
        raise SyncError(f"{board}: config generator diagnostic\n{result.stderr}")
    output = directory / "Makefile"
    if not output.is_file():
        raise SyncError(f"{board}: generator produced no Makefile")
    return output.read_bytes()


def compare(root, workspace, generator):
    hashes = {}
    differences = []
    for board in BOARDS:
        relative = COMPILE / board / "Makefile"
        expected = root / relative
        if not expected.is_file():
            raise SyncError(f"{board}: missing tracked output {relative}")
        original = expected.read_bytes()
        regenerated = generate(generator, workspace / COMPILE / board, board)
        hashes[board] = hashlib.sha256(regenerated).hexdigest()
        if original != regenerated:
            difference = "".join(difflib.unified_diff(
                original.decode("utf-8", errors="replace").splitlines(keepends=True),
                regenerated.decode("utf-8", errors="replace").splitlines(keepends=True),
                fromfile=str(relative), tofile=f"regenerated/{relative}",
            ))
            differences.append(f"{board}: generated Makefile differs byte for byte\n{difference}")
    if differences:
        raise SyncError("\n".join(differences))
    return hashes


def check(root, make="bmake"):
    with tempfile.TemporaryDirectory(prefix="discobsd-config-sync-") as temporary:
        workspace = Path(temporary)
        copy_inputs(root, workspace)
        generator = build_generator(workspace, make)
        return compare(root, workspace, generator)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--make", default="bmake")
    args = parser.parse_args()
    try:
        hashes = check(args.root.resolve(), args.make)
    except (SyncError, OSError) as error:
        print(f"ERROR config-generated-sync: {error}", file=sys.stderr)
        return 1
    for board, digest in hashes.items():
        print(f"PASS config-generated-sync {board}: byte-identical sha256={digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
