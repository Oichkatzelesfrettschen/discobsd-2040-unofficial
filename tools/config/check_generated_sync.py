"""Compare production RP2040 kernel configuration files with isolated config output."""

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
BOARDS = ("PICO", "PICO_DIAG", "PICO_UART", "PICOCALC", "PICOCALC_LEAN",
          "PICOCALC_LEAN_DIAG", "PICOCALC_SD", "PICOCALC_SD_DIAG")
# The diagnostic options a _DIAG kernel adds to its lean base; a further
# diagnostic option joins this tuple.
DIAGNOSTIC_OPTIONS = ("options SYSTRACE", "options CAPACITY_STATS")
# The SD-root kernel's diagnostic additions to PICOCALC: the syscall trace
# and the panel register test. The panel test runs through /dev/lcdgfx, so
# it needs DEV_LCDGFX, which PICOCALC_SD inherits and PICOCALC_LEAN_DIAG
# omits; both options leave the 128 KiB PICOCALC image for room.
SD_PANEL_OPTIONS = ("options SYSTRACE", "options LCD_PANELTEST")
# The per-driver device options the full PICOCALC kernel carries and
# PICOCALC_LEAN omits.
OPTIONAL_DEVICE_OPTIONS = ("options DEV_ADC", "options DEV_GPIO",
                           "options DEV_LCDGFX", "options DEV_PCM")
# The additions of PICOCALC_SD_DIAG, the testing kernel: the signal-path
# prints, the exec trace of sys/arch/rp2040/include/debug.h, the storage
# counters, the diagnostic counters and message ring (sys/sys/diag_stats.h),
# the console mirror on UART0 (sys/dev/cons.c) and a -Og compile
# (makeoptions COPTS reaches Makefile.rp2040's COPTS?= default). Sources
# test -DPICOCALC_SD (dev/flash.h, dev/sd.c, dev/uart.c) and a board line
# defines only its own name, so the variant defines PICOCALC_SD with an
# option.
SD_DIAG_OPTIONS = ("options PICOCALC_SD", "options DIAGNOSTIC",
                   'options "GLOBAL_DEBUG=9"', "options STORAGE_STATS",
                   "options DIAG_STATS", "options CONSOLE_MIRROR_UART",
                   'makeoptions COPTS="-Og"')
# (variant, base, directives the variant adds to the base Config, directives
# it removes from it).
VARIANTS = (
    ("PICO_DIAG", "PICO", DIAGNOSTIC_OPTIONS, ()),
    ("PICOCALC_LEAN", "PICOCALC", (),
     OPTIONAL_DEVICE_OPTIONS + ("options CAPACITY_STATS",)),
    ("PICOCALC_LEAN_DIAG", "PICOCALC_LEAN", DIAGNOSTIC_OPTIONS, ()),
    ("PICOCALC_SD", "PICOCALC",
     ('ldscript "RP2040-sd.ld"', 'config unix root on sd0a', *SD_PANEL_OPTIONS,
      'options LCD_GFX_EXT', 'options DEV_GPIO_PWM', 'options LCD_VT',
      'options "NPROC=32"', 'options "MAXUPRC=25"', 'options "SMAPSIZ=36"'),
     ('ldscript "RP2040.ld"', 'config unix root on fl0a')),
    ("PICOCALC_SD_DIAG", "PICOCALC_SD", SD_DIAG_OPTIONS, ()),
)
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
            for name in ("Makefile", "ioconf.c", "swapunix.c"):
                copy_file(root, destination, COMPILE / board / name)


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


def option_lines(path):
    """Return a Config's directive lines without comments or blank lines."""
    lines = []
    for line in path.read_text().splitlines():
        directive = line.split("#", 1)[0].strip()
        if directive:
            lines.append(re.sub(r"\s+", " ", directive))
    return lines


def check_variant_delta(root):
    """Hold a variant Config to its base Config plus the variant's own lines.

    config(8) lacks an include directive, so each variant repeats its base. The
    comparison is by directive: the board line carries the variant's name and
    the listed directives are the only additions and removals. A removal the
    base Config does not carry is refused, so a variant cannot name a cut
    that no longer separates it from its base.
    """
    for variant, base, additions, removals in VARIANTS:
        base_lines = option_lines(root / COMPILE / base / "Config")
        for removal in removals:
            if removal not in base_lines:
                raise SyncError(f"{variant}: {base} Config lacks the directive "
                                f"{removal} that the variant removes")
        expected = [
            f'board "{variant}"' if line == f'board "{base}"' else line
            for line in base_lines if line not in removals
        ] + list(additions)
        actual = option_lines(root / COMPILE / variant / "Config")
        if sorted(expected) != sorted(actual):
            delta = ([f"plus {line}" for line in additions]
                     + [f"minus {line}" for line in removals]) or ["unchanged"]
            raise SyncError(
                f"{variant}: Config must equal {base} Config ({', '.join(delta)}) "
                f"and its own board line\n"
                + "".join(difflib.unified_diff(
                    [line + "\n" for line in sorted(expected)],
                    [line + "\n" for line in sorted(actual)],
                    fromfile=f"{base} plus delta", tofile=f"{variant} Config")))


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
        for name in ("ioconf.c", "swapunix.c"):
            compiled = COMPILE / board / name
            if not (root / compiled).is_file() or not (workspace / compiled).is_file():
                raise SyncError(f"{board}: missing generated {name}")
            if (root / compiled).read_bytes() != (workspace / compiled).read_bytes():
                differences.append(f"{board}: generated {name} differs byte for byte")
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
        check_variant_delta(workspace)
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
