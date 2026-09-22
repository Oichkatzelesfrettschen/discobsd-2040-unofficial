"""Exercise production dependency barriers with controller-held prerequisites."""

import argparse
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import check_generated_sync as sync

SCRIPT = Path(__file__).resolve()
LINKS = ("machine", "sys", ".deps")


def assignment(contents):
    lines = contents.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith("SYSTEM_DEP="):
            result = line
            while result.endswith("\\\n"):
                index += 1
                result += lines[index]
            return result
    raise sync.SyncError("SYSTEM_DEP assignment is absent")


def link_recipes(contents):
    recipes = {}
    for target in LINKS:
        match = re.search(rf"^{re.escape(target)}:\n((?:\t[^\n]*\n)+)",
                          contents, re.MULTILINE)
        if not match:
            raise sync.SyncError(f"{target}: production recipe is absent")
        recipes[target] = match.group(1)
    return recipes


def stamp(name):
    Path(name).write_text(str(time.monotonic_ns()))


def wait_for(predicate, process=None):
    deadline = time.monotonic() + 10
    while not predicate():
        if process is not None and process.poll() is not None:
            raise sync.SyncError("ordering fixture stopped before the required event")
        if time.monotonic() >= deadline:
            raise sync.SyncError("ordering fixture event deadline expired")
        time.sleep(0.005)


def actor(operation, name):
    allowed = {
        "config-start": {"config"}, "config-done": {"config"},
        "link-start": set(LINKS), "link-done": set(LINKS),
        "compile": {"probe.o", "swapunix.o"},
    }
    if name not in allowed.get(operation, set()):
        raise sync.SyncError("invalid ordering-fixture actor")
    if operation == "config-start":
        stamp("config.started")
        wait_for(lambda: Path("release-config").exists())
    elif operation == "config-done":
        stamp("config.done")
    elif operation == "link-start":
        if not Path("config.done").exists():
            Path("fault").write_text("premature-links")
            return 1
        stamp(name + ".started")
        wait_for(lambda: Path("release-links").exists())
    elif operation == "link-done":
        stamp(name + ".done")
    elif operation == "compile":
        stamp(name + ".started")
        if not all(Path(link + ".done").exists() for link in LINKS):
            Path("fault").write_text("premature-compile")
            return 1
    return 0


def exercise(dependencies, recipes, target, make, mutation=None):
    with tempfile.TemporaryDirectory(prefix="discobsd-include-order-") as temporary:
        directory = Path(temporary)
        for relative in ("source/arch/rp2040/include", "source/sys", "conf"):
            (directory / relative).mkdir(parents=True)
        (directory / "source/arch/rp2040/include/types.h").write_text("typedef int fixture_type;\n")
        (directory / "source/sys/token.h").write_text("#define FIXTURE_VALUE 7\n")
        for name in ("fixture.ld", "kern.ldscript"):
            (directory / "conf" / name).write_text("")
        (directory / "probe.c").write_text(
            "#include <machine/types.h>\n#include <sys/token.h>\n"
            "fixture_type fixture_value = FIXTURE_VALUE;\n"
        )
        if mutation == "config":
            dependencies = dependencies.replace(".WAIT", "", 1)
        elif mutation == "links":
            prefix, suffix = dependencies.rsplit(".WAIT", 1)
            dependencies = prefix + suffix
        invocation = shlex.join([sys.executable, str(SCRIPT), "--actor"]).replace("$", "$$")
        compiler = shlex.join(shlex.split(os.environ.get("HOST_CC", "cc"))).replace("$", "$$")
        contents = (
            ".MAIN: unix\nS=source\n_machdir=source/arch/rp2040\n"
            '_confdir=conf\nLDSCRIPT="fixture.ld"\nSYSTEM_OBJ=probe.o\n'
            + dependencies + target + "\n\t@touch unix\n"
            "ioconf.c:\n"
            f"\t@{invocation} config-start config\n"
            "\t@touch ioconf.c\n"
            f"\t@{invocation} config-done config\n"
        )
        for link, recipe in recipes.items():
            contents += (
                f"{link}:\n\t@{invocation} link-start {link}\n"
                + recipe + f"\t@{invocation} link-done {link}\n"
            )
        contents += (
            "probe.o swapunix.o: probe.c\n"
            f"\t@{invocation} compile $@\n"
            f"\t{compiler} -std=c17 -Wall -Wextra -Werror -I. -c probe.c -o $@\n"
        )
        (directory / "Makefile").write_text(contents)
        with tempfile.TemporaryFile(mode="w+") as output:
            process = subprocess.Popen([make, "-j8", "unix"], cwd=directory,
                                       env=sync.child_environment(), stdout=output,
                                       stderr=subprocess.STDOUT)
            try:
                wait_for(lambda: (directory / "config.started").exists(), process)
                if mutation == "config":
                    wait_for(lambda: (directory / "fault").exists(), process)
                else:
                    if any((directory / (link + ".started")).exists() for link in LINKS):
                        raise sync.SyncError("include creation overtook the held configuration")
                    (directory / "release-config").touch()
                    wait_for(lambda: all((directory / (link + ".started")).exists()
                                        for link in LINKS), process)
                    if mutation == "links":
                        wait_for(lambda: (directory / "fault").exists(), process)
                    elif any((directory / (name + ".started")).exists()
                             for name in ("probe.o", "swapunix.o")):
                        raise sync.SyncError("compilation overtook held include creation")
            finally:
                (directory / "release-config").touch()
                (directory / "release-links").touch()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise sync.SyncError("ordering fixture make failed to terminate") from None
            output.seek(0)
            log = output.read()
        fault = (directory / "fault").read_text() if (directory / "fault").exists() else None
        if mutation:
            expected = "premature-links" if mutation == "config" else "premature-compile"
            if process.returncode == 0 or fault != expected:
                raise sync.SyncError(f"{mutation}: negative control lacks {expected}\n{log}")
        elif process.returncode or fault or not (directory / "unix").exists():
            raise sync.SyncError(f"ordering fixture rejected intact barriers\n{log}")


def check(root, make):
    target_lines = []
    sources = [sync.CONF / "Makefile.rp2040"] + [
        sync.COMPILE / board / "Makefile" for board in sync.BOARDS
    ]
    for board in sync.BOARDS:
        contents = (root / sync.COMPILE / board / "Makefile").read_text()
        match = re.search(r"^unix: \$\{SYSTEM_DEP\} swapunix.o$", contents, re.MULTILINE)
        if not match:
            raise sync.SyncError(f"{board}: generated unix dependency rule changed")
        target_lines.append(match.group(0))
    for source in sources:
        contents = (root / source).read_text()
        dependencies = assignment(contents)
        groups = dependencies.split("=", 1)[1].replace("\\\n", " ").split(".WAIT")
        if len(groups) != 3 or groups[0].split() != ["Makefile", "ioconf.c"] or (
            groups[1].split() != list(LINKS) or "${SYSTEM_OBJ}" not in groups[2].split()
        ):
            raise sync.SyncError(f"{source}: SYSTEM_DEP must order config, include links, objects")
        for mutation in (None, "config", "links"):
            exercise(dependencies, link_recipes(contents), target_lines[0], make, mutation)
        print(f"PASS include-order {source}: held prerequisites and both missing-barrier controls")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=sync.ROOT)
    parser.add_argument("--make", default="bmake")
    parser.add_argument("--actor", nargs=2, metavar=("OPERATION", "NAME"))
    args = parser.parse_args()
    try:
        if args.actor:
            return actor(*args.actor)
        check(args.root.resolve(), args.make)
    except (sync.SyncError, OSError) as error:
        print(f"ERROR include-order: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
