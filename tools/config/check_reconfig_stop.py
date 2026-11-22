# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Check that a kernel Makefile refuses to build from a Config it does not match.

config writes CONFIG_CKSUM, the POSIX cksum of the Config it read, into each
compile directory's Makefile, and the skeletons Makefile.rp2040 and
Makefile.stm32 compare it with Config when make reads the Makefile. Every
tracked compile Makefile must carry its Config's cksum, which also checks
config's CRC against the system's cksum. Each skeleton's check runs in a
scratch directory whose fake config rewrites CONFIG_CKSUM, through six
scenarios; four mutants of the check must each fail the scenario named for
them.
"""

import argparse
import os
import re
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

import check_generated_sync as sync

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = {
    "sys/arch/rp2040/conf/Makefile.rp2040": "sys/arch/rp2040/compile",
    "sys/arch/stm32/conf/Makefile.stm32": "sys/arch/stm32/compile",
}
START = ".if !make(clean) && !make(clean-all) && !make(reconfig)\n"
END = "reconfig ioconf.c: Config ${CONFIG_TOOL}\n\t${CONFIG_TOOL} Config\n\t${MAKE} clean\n"
FAKE_CONFIG = """#!/bin/sh
# Rewrite CONFIG_CKSUM from Config, as config rewrites the generated Makefile.
set -eu
sum=$(cksum < Config)
sed "s/^CONFIG_CKSUM = .*/CONFIG_CKSUM = $sum/" Makefile > Makefile.new
mv Makefile.new Makefile
cksum < Config > ioconf.c
"""
STALE = "changed since config wrote this Makefile"
ALONE = "run it alone"


class GateError(Exception):
    pass


def cksum(path):
    with open(path, "rb") as f:
        return subprocess.run(["cksum"], stdin=f, stdout=subprocess.PIPE, text=True,
                              check=True).stdout.strip()


def check_block(contents, name):
    start = contents.find(START)
    end = contents.find(END, start)
    if start < 0 or end < 0:
        raise GateError(f"{name}: the Config check or the reconfig rule is absent")
    return contents[start:end + len(END)]


def mutate(text, old, new, name):
    if old not in text:
        raise GateError(f"mutant {name}: its anchor is absent from the check")
    return text.replace(old, new, 1)


def mutants(text):
    """(name, check, the scenario that must fail)."""
    return (
        ("no cksum test", mutate(text, '.if "${_CONFIG_CKSUM}" != "${CONFIG_CKSUM}"', ".if 0",
                                 "cksum"), "changed"),
        ("reconfig refused", mutate(text, " && !make(reconfig)\n", "\n", "reconfig"),
         "reconfig alone"),
        ("reconfig beside a build", mutate(text, "${.TARGETS:[#]} != 1", "0", "alone"),
         "reconfig with all"),
        ("clean refused", mutate(text, "!make(clean) && ", "", "clean"), "clean"),
    )


class Fixture:
    def __init__(self, directory, block, make, config):
        self.dir, self.make = directory, make
        tool = directory / "fake-config"
        tool.write_text(FAKE_CONFIG)
        tool.chmod(tool.stat().st_mode | stat.S_IXUSR)
        (directory / "Config").write_text("OLD\n")
        (directory / "Makefile").write_text(
            "CONFIG_CKSUM = " + cksum(directory / "Config") + "\n.MAIN: all\n"
            "CONFIG_TOOL=" + str(tool) + "\n"
            "all: ioconf.c\n\t@cat ioconf.c > built\n"
            "clean clean-all:\n\t@rm -f built\n" + block)
        (directory / "ioconf.c").write_text("")
        if config is not None:
            (directory / "Config").write_text(config)

    def run(self, *goals):
        result = subprocess.run([self.make, *goals], cwd=self.dir, env=sync.child_environment(),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                timeout=60, check=False)
        return result.returncode, result.stdout

    def built(self):
        return (self.dir / "built").exists()


def scenarios(block, make):
    """Return the name of the first scenario the check fails, or None."""
    def fixture(scratch, config=None):
        return Fixture(Path(scratch), block, make, config)

    with tempfile.TemporaryDirectory() as scratch:
        f = fixture(scratch)
        rc, _ = f.run("all")
        if rc != 0 or not f.built():
            return "unchanged"
    with tempfile.TemporaryDirectory() as scratch:
        f = fixture(scratch, "OLD\n")
        config_time = (f.dir / "Config").stat().st_mtime
        os.utime(f.dir / "ioconf.c", (config_time - 10, config_time - 10))
        rc, _ = f.run("all")
        if rc != 0 or not f.built() or (f.dir / "built").read_text().strip() != cksum(
                f.dir / "Config"):
            return "touched"
    for mode in ((), ("-j4",)):
        with tempfile.TemporaryDirectory() as scratch:
            f = fixture(scratch, "NEW\n")
            rc, out = f.run(*mode, "all")
            if rc == 0 or STALE not in out or f.built():
                return "changed"
    with tempfile.TemporaryDirectory() as scratch:
        f = fixture(scratch, "NEW\n")
        rc, _ = f.run("reconfig")
        rc2, _ = f.run("all")
        if rc != 0 or rc2 != 0 or not f.built():
            return "reconfig alone"
    with tempfile.TemporaryDirectory() as scratch:
        f = fixture(scratch, "NEW\n")
        rc, out = f.run("reconfig", "all")
        if rc == 0 or ALONE not in out or f.built():
            return "reconfig with all"
    with tempfile.TemporaryDirectory() as scratch:
        f = fixture(scratch, "NEW\n")
        rc, _ = f.run("clean")
        if rc != 0:
            return "clean"
    return None


def check(root, make):
    for template, compile_dir in TEMPLATES.items():
        block = check_block((root / template).read_text(), template)
        failed = scenarios(block, make)
        if failed is not None:
            raise GateError(f"{template}: the check fails the {failed} scenario")
        for name, mutant, expected in mutants(block):
            failed = scenarios(mutant, make)
            if failed != expected:
                raise GateError(f"{template}: mutant {name} failed {failed!r}, "
                                f"expected {expected!r}")
        generated = sorted((root / compile_dir).glob("*/Makefile"))
        if not generated:
            raise GateError(f"{compile_dir}: no generated Makefile")
        for path in generated:
            name = str(path.relative_to(root))
            contents = path.read_text()
            if check_block(contents, name) != block:
                raise GateError(f"{name}: Config check differs from {template}")
            match = re.search(r"^CONFIG_CKSUM = (.*)$", contents, re.M)
            want = cksum(path.parent / "Config")
            if match is None or match.group(1) != want:
                raise GateError(f"{name}: CONFIG_CKSUM is not cksum of Config ({want})")
        print(f"PASS reconfig-stop {template}: six scenarios, four mutants, "
              f"{len(generated)} generated Makefiles match their Config")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--make", default="bmake")
    args = parser.parse_args()
    try:
        check(args.root.resolve(), args.make)
    except (GateError, sync.SyncError, OSError, subprocess.SubprocessError) as error:
        print(f"ERROR reconfig-stop: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
