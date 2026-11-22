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

"""Run the reconfig rule of each kernel Makefile template against a fake config.

config rewrites a compile directory's Makefile while the make that runs it has
already read the old one. The rule must stop a build when the rewrite changed
the file, let an identical rewrite go on, and pass "make reconfig" asked for
with no other goal. Each production template's rule runs in a scratch
directory whose fake config rewrites PARAM from Config and refuses to run
twice at once. Three mutants of the rule must each fail the scenario named
for them, and every tracked generated Makefile must carry its template's rule
verbatim, .ORDER line included.
"""

import argparse
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
RULE = re.compile(r"^\.ORDER: reconfig ioconf\.c\nreconfig ioconf\.c: Config \$\{CONFIG_TOOL\}\n"
                  r"((?:\t[^\n]*\n)+)", re.MULTILINE)
PARALLEL_RUNS = 3
FAKE_CONFIG = """#!/bin/sh
# Rewrite PARAM from Config, as config rewrites the generated Makefile. Two
# runs that overlap fail: the second finds the first one's lock directory.
set -eu
mkdir config.lock
param=$(cat Config)
sleep 0.3
sed "s/^PARAM=.*/PARAM=$param/" Makefile > Makefile.new
mv Makefile.new Makefile
echo "/* $param */" > ioconf.c
rmdir config.lock
"""
STOP = "rewrote"


class GateError(Exception):
    pass


def rule(contents, name):
    match = RULE.search(contents)
    if match is None:
        raise GateError(f"{name}: reconfig rule is absent")
    return match.group(0)


def mutate(text, old, new, name):
    if old not in text:
        raise GateError(f"mutant {name}: its anchor is absent from the rule")
    return text.replace(old, new, 1)


def mutants(text):
    """(name, rule, the scenario that must fail)."""
    old_rule = ("reconfig ioconf.c: Config ${CONFIG_TOOL}\n"
                "\t${CONFIG_TOOL} Config\n\t${MAKE} clean\n")
    return (
        ("no stop", old_rule, "changed"),
        ("goal of the recipe", mutate(text, "${.TARGETS}", "${.TARGET}", "goal"),
         "reconfig with all"),
        ("stop always", mutate(text, '! cmp -s Makefile "$$before"', "true", "always"),
         "unchanged"),
    )


class Fixture:
    def __init__(self, directory, rule_text, make):
        self.dir, self.make = directory, make
        tool = directory / "fake-config"
        tool.write_text(FAKE_CONFIG)
        tool.chmod(tool.stat().st_mode | stat.S_IXUSR)
        (directory / "Makefile").write_text(
            ".MAIN: all\nPARAM=OLD\nCONFIG_TOOL=" + str(tool) + "\n"
            "all: ioconf.c\n\t@echo ${PARAM} > built\n"
            "clean:\n\t@rm -f built\n" + rule_text)

    def config(self, param):
        (self.dir / "ioconf.c").unlink(missing_ok=True)
        (self.dir / "Config").write_text(param + "\n")

    def run(self, *goals):
        result = subprocess.run([self.make, *goals], cwd=self.dir, env=sync.child_environment(),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                timeout=60, check=False)
        return result.returncode, result.stdout

    def built(self):
        path = self.dir / "built"
        return path.read_text().strip() if path.exists() else None


def scenarios(rule_text, make):
    """Return the name of the first scenario the rule fails, or None."""
    with tempfile.TemporaryDirectory() as scratch:
        f = Fixture(Path(scratch), rule_text, make)
        f.config("OLD")
        rc, _ = f.run("all")
        if rc != 0 or f.built() != "OLD":
            return "unchanged"
    with tempfile.TemporaryDirectory() as scratch:
        f = Fixture(Path(scratch), rule_text, make)
        f.config("NEW")
        rc, out = f.run("all")
        if rc == 0 or STOP not in out or f.built() is not None:
            return "changed"
        rc, _ = f.run("all")
        if rc != 0 or f.built() != "NEW":
            return "changed, second build"
    with tempfile.TemporaryDirectory() as scratch:
        f = Fixture(Path(scratch), rule_text, make)
        f.config("NEW")
        rc, _ = f.run("reconfig")
        if rc != 0 or "PARAM=NEW" not in (f.dir / "Makefile").read_text():
            return "reconfig alone"
    with tempfile.TemporaryDirectory() as scratch:
        f = Fixture(Path(scratch), rule_text, make)
        f.config("NEW")
        rc, _ = f.run("reconfig", "all")
        if rc == 0:
            return "reconfig with all"
    # Named beside a build under -j, reconfig and ioconf.c are both out of date and share the
    # recipe; its two runs must not overlap (the fake config's lock), and an unchanged rewrite
    # must still build. Without the .ORDER line the runs overlap only sometimes, so RULE
    # requires that line instead of a mutant testing it.
    for _ in range(PARALLEL_RUNS):
        with tempfile.TemporaryDirectory() as scratch:
            f = Fixture(Path(scratch), rule_text, make)
            f.config("OLD")
            rc, _ = f.run("-j4", "reconfig", "all")
            if rc != 0 or f.built() != "OLD" or list(f.dir.glob(".Makefile.before-config*")):
                return "parallel reconfig with all"
    return None


def check(root, make):
    for template, compile_dir in TEMPLATES.items():
        text = rule((root / template).read_text(), template)
        failed = scenarios(text, make)
        if failed is not None:
            raise GateError(f"{template}: the rule fails the {failed} scenario")
        for name, mutant, expected in mutants(text):
            failed = scenarios(mutant, make)
            if failed != expected:
                raise GateError(f"{template}: mutant {name} failed {failed!r}, "
                                f"expected {expected!r}")
        generated = sorted((root / compile_dir).glob("*/Makefile"))
        if not generated:
            raise GateError(f"{compile_dir}: no generated Makefile")
        for path in generated:
            if rule(path.read_text(), str(path.relative_to(root))) != text:
                raise GateError(f"{path.relative_to(root)}: reconfig rule differs from {template}")
        print(f"PASS reconfig-stop {template}: five scenarios, three mutants, "
              f"{len(generated)} generated Makefiles")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--make", default="bmake")
    args = parser.parse_args()
    try:
        check(args.root.resolve(), args.make)
    except (GateError, sync.SyncError, OSError, subprocess.TimeoutExpired) as error:
        print(f"ERROR reconfig-stop: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
