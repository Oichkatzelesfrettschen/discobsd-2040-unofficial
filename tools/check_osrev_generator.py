#!/usr/bin/env python3
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
#
# Gate for the packed release number (kern.osrev) that sys/conf/newvers.sh
# generates from OSR and OSRANK.
#
# The gate runs a copy of newvers.sh, with OSR and OSRANK replaced, in a
# directory with no repository, and compiles the vers.c it writes with the host
# compiler under -Wall -Wextra -Werror, as the kernel build does. It requires:
#   - the packed value of each good version to equal an independent encoder,
#     and a decoder to return its fields;
#   - the values of one release cycle to rise strictly in precedence order,
#     every stage below its release;
#   - each malformed OSR or OSRANK to stop the script with a message and no
#     standard output, so the link keeps the last good vers.c;
#   - each edit that separates one generated macro from the others to stop the
#     compile at the _Static_assert that names it;
#   - the OSR and OSRANK of the tree to generate a vers.c that compiles.
# --selftest mutates the rules the verdict rests on and requires each mutant to
# fail the check it was built to fail.
#
# usage: check_osrev_generator.py --root TOPSRC
#        check_osrev_generator.py --selftest

import argparse
import os
import re
import subprocess
import sys
import tempfile

MAJOR_WEIGHT = 100000000
MINOR_WEIGHT = 1000000
PATCH_WEIGHT = 10
RELEASE_STAGE = 9
LONG_MAX = 2**31 - 1

# (OSR, OSRANK) pairs the generator accepts, with the stage digit each carries.
GOOD = [
    ("0.1.0-alpha", "alpha=1"),
    ("0.1.0-beta", "beta=2"),
    ("0.1.0", ""),
    ("0.99.99999-beta", "beta=2"),
    ("20.99.99999", ""),
    ("1.0.0", ""),
    ("0.1.0-rc", "rc=5"),
]

# One release cycle in precedence order; every value must exceed the one before.
CYCLE = [
    ("0.1.0-alpha", "alpha=1"),
    ("0.1.0-beta", "beta=2"),
    ("0.1.0", ""),
    ("0.1.1-alpha", "alpha=1"),
    ("0.1.1", ""),
    ("0.1.2", ""),
    ("0.2.0-alpha", "alpha=1"),
    ("0.2.0", ""),
    ("1.0.0-alpha", "alpha=1"),
    ("1.0.0", ""),
    ("20.99.99999", ""),
]

# Malformed sources: the script exits nonzero, names OSR or OSRANK, and writes
# nothing to standard output.
REJECTED = [
    ("21.0.0", ""),
    ("0.100.0", ""),
    ("0.1.100000", ""),
    ("0.1.0-gamma", "alpha=1"),
    ("0.1.0-alpha.1", "alpha=1"),
    ("0.01.0", ""),
    ("0.1.0+202611", ""),
    ("0.1", ""),
    ("0.1.0.0", ""),
    ("", ""),
    ("0.1.0000000007", ""),
    ("0.1.0-", ""),
    ("0.1.0-Alpha", "Alpha=1"),
    ("0.1.0-1", "1=1"),
    ("0.1.-1", ""),
    ("0.1.08", ""),
    ("a.1.0", ""),
    ("0.1.0-alpha", ""),
    ("0.1.0-alpha", "alpha=0"),
    ("0.1.0-alpha", "alpha=9"),
    ("0.1.0-alpha", "alpha=12"),
    ("0.1.0-alpha", "beta=2"),
    ("0.1.0-alpha", "alpha"),
    ("0.1.0", "alpha=1"),
]

# Edits to a good vers.c that must stop the compile at the named assertion.
EDITS = [
    ("#define OSREV_PACKED", "#define OSREV_PACKED\t1000002L #", "packed value disagrees"),
    ("#define OSREV_STAGE", "#define OSREV_STAGE\t2 //", "packed value disagrees"),
    ("#define OSREV_MINOR", "#define OSREV_MINOR\t2 //", "packed value disagrees"),
    ("#define OSREV_STRING", '#define OSREV_STRING\t"0.1.0-beta" //', "string disagrees"),
    ("#define OSREV_PRE", '#define OSREV_PRE\t"" //', "stage disagrees with the label"),
    ("#define OSREV_MAJOR", "#define OSREV_MAJOR\t21 //", "major exceeds 20"),
    ("#define OSREV_MINOR", "#define OSREV_MINOR\t100 //", "minor exceeds 99"),
    ("#define OSREV_PATCH", "#define OSREV_PATCH\t100000 //", "patch exceeds 99999"),
    ("#define OSREV_STAGE", "#define OSREV_STAGE\t0 //", "stage is not from 1 to 9"),
]


def encode(major, minor, patch, stage):
    """The decimal layout of docs/research/version-scheme.md, written apart from the script."""
    return major * MAJOR_WEIGHT + minor * MINOR_WEIGHT + patch * PATCH_WEIGHT + stage


def decode(value):
    return (value // MAJOR_WEIGHT, (value // MINOR_WEIGHT) % 100,
            (value % MINOR_WEIGHT) // PATCH_WEIGHT, value % 10)


def fields(osr, rank):
    """The fields a good OSR and OSRANK stand for, read without the script's code."""
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)(?:-([a-z]+))?", osr)
    if match is None:
        raise ValueError(osr)
    major, minor, patch = (int(match.group(n)) for n in (1, 2, 3))
    label = match.group(4)
    stage = RELEASE_STAGE if label is None else int(rank.split("=")[1])
    return major, minor, patch, stage


def source_with(root, osr, rank, where):
    """Copy newvers.sh into where with OSR and OSRANK replaced; return its path."""
    text = open(os.path.join(root, "sys", "conf", "newvers.sh"), encoding="utf-8").read()
    for name, value in (("OSR", osr), ("OSRANK", rank)):
        text, count = re.subn(rf'^{name}=".*"$', lambda _m, n=name, v=value: f'{n}="{v}"',
                              text, count=1, flags=re.MULTILINE)
        if count != 1:
            raise RuntimeError(f"newvers.sh has no {name}= line to replace")
    path = os.path.join(where, "sys", "conf", "newvers.sh")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return path


def generate(script, workdir):
    """Run the copied script in a directory with no repository."""
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "SOURCE_DATE_EPOCH": "0",
           "LC_ALL": "C"}
    return subprocess.run(["sh", script], cwd=workdir, env=env, capture_output=True,
                          text=True, check=False)


def compile_vers(vers_c, workdir):
    cc = os.environ.get("CC", "cc")
    return subprocess.run([cc, "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-c", vers_c,
                           "-o", os.devnull], cwd=workdir, capture_output=True, text=True,
                          check=False)


def packed_of(vers_text):
    match = re.search(r"^const long osrev = OSREV_PACKED;$", vers_text, re.MULTILINE)
    value = re.search(r"^#define OSREV_PACKED\t(\d+)L$", vers_text, re.MULTILINE)
    if match is None or value is None:
        return None
    return int(value.group(1))


def check_good(root, scratch):
    defects = []
    for osr, rank in GOOD:
        where = tempfile.mkdtemp(dir=scratch)
        script = source_with(root, osr, rank, where)
        build = os.path.join(where, "build")
        os.makedirs(build)
        done = generate(script, build)
        if done.returncode != 0 or done.stderr:
            defects.append(f"{osr!r} {rank!r}: the script failed or wrote to stderr: "
                           f"{done.stderr!r}")
            continue
        vers_c = os.path.join(build, "vers.c")
        with open(vers_c, "w", encoding="utf-8") as handle:
            handle.write(done.stdout)
        built = compile_vers(vers_c, build)
        if built.returncode != 0:
            defects.append(f"{osr!r} {rank!r}: vers.c does not compile: "
                           f"{built.stderr.strip()[:200]}")
            continue
        want = fields(osr, rank)
        got = packed_of(done.stdout)
        if got != encode(*want):
            defects.append(f"{osr!r} {rank!r}: packed {got}, the encoder gives {encode(*want)}")
        elif decode(got) != want:
            defects.append(f"{osr!r} {rank!r}: the decoder returns {decode(got)}, not {want}")
        elif not 0 < got <= LONG_MAX:
            defects.append(f"{osr!r} {rank!r}: {got} is outside a signed 32-bit long")
        if "const char osrelease[] = OSREV_STRING;" not in done.stdout:
            defects.append(f"{osr!r}: osrelease is not the string of the packed fields")
    return defects


def check_cycle(root, scratch):
    defects = []
    previous = None
    for osr, rank in CYCLE:
        where = tempfile.mkdtemp(dir=scratch)
        script = source_with(root, osr, rank, where)
        build = os.path.join(where, "build")
        os.makedirs(build)
        done = generate(script, build)
        value = packed_of(done.stdout) if done.returncode == 0 else None
        if value is None:
            defects.append(f"{osr!r}: no packed value")
            continue
        if previous is not None and value <= previous[1]:
            defects.append(f"{osr!r} packs to {value}, not above {previous[0]!r} ({previous[1]})")
        previous = (osr, value)
    return defects


def check_rejected(root, scratch):
    defects = []
    for osr, rank in REJECTED:
        where = tempfile.mkdtemp(dir=scratch)
        script = source_with(root, osr, rank, where)
        build = os.path.join(where, "build")
        os.makedirs(build)
        done = generate(script, build)
        if done.returncode == 0:
            defects.append(f"{osr!r} {rank!r}: accepted")
        elif done.stdout:
            defects.append(f"{osr!r} {rank!r}: rejected after writing {len(done.stdout)} bytes")
        elif "OSR" not in done.stderr:
            defects.append(f"{osr!r} {rank!r}: rejected without naming OSR or OSRANK: "
                           f"{done.stderr!r}")
        elif os.path.exists(os.path.join(build, ".compileversion")):
            defects.append(f"{osr!r} {rank!r}: rejected after touching .compileversion")
    return defects


def check_edits(root, scratch):
    defects = []
    where = tempfile.mkdtemp(dir=scratch)
    script = source_with(root, "0.1.0-alpha", "alpha=1", where)
    build = os.path.join(where, "build")
    os.makedirs(build)
    good = generate(script, build).stdout
    for prefix, replacement, message in EDITS:
        edited = []
        count = 0
        for line in good.split("\n"):
            if line.startswith(prefix):
                line = replacement
                count += 1
            edited.append(line)
        if count != 1:
            defects.append(f"edit {prefix!r}: found on {count} lines")
            continue
        vers_c = os.path.join(build, "edited.c")
        with open(vers_c, "w", encoding="utf-8") as handle:
            handle.write("\n".join(edited))
        built = compile_vers(vers_c, build)
        if built.returncode == 0:
            defects.append(f"edit {replacement!r}: compiled")
        elif message not in built.stderr:
            defects.append(f"edit {replacement!r}: failed without {message!r}: "
                           f"{built.stderr.strip()[:160]}")
    return defects


def check_tree(root, scratch):
    """The version source of the tree itself generates a vers.c that compiles."""
    text = open(os.path.join(root, "sys", "conf", "newvers.sh"), encoding="utf-8").read()
    osr = re.search(r'^OSR="(.*)"$', text, re.MULTILINE)
    rank = re.search(r'^OSRANK="(.*)"$', text, re.MULTILINE)
    if osr is None or rank is None:
        return ["newvers.sh has no OSR or OSRANK line"]
    where = tempfile.mkdtemp(dir=scratch)
    script = source_with(root, osr.group(1), rank.group(1), where)
    build = os.path.join(where, "build")
    os.makedirs(build)
    done = generate(script, build)
    if done.returncode != 0:
        return [f"the tree's OSR {osr.group(1)!r} and OSRANK {rank.group(1)!r} are rejected: "
                f"{done.stderr.strip()}"]
    vers_c = os.path.join(build, "vers.c")
    with open(vers_c, "w", encoding="utf-8") as handle:
        handle.write(done.stdout)
    built = compile_vers(vers_c, build)
    if built.returncode != 0:
        return [f"the tree's vers.c does not compile: {built.stderr.strip()[:200]}"]
    print(f"check-osrev-generator: the tree's OSR {osr.group(1)} packs to {packed_of(done.stdout)}")
    return []


def verdict(root):
    """The defects found, each paired with the name of the check that found it."""
    defects = []
    with tempfile.TemporaryDirectory() as scratch:
        for check in (check_good, check_cycle, check_rejected, check_edits, check_tree):
            defects += [(check.__name__, text) for text in check(root, scratch)]
    return defects


def selftest(root):
    """Calibrate the verdict against the tree's script and mutants of it."""
    base = verdict(root)
    if base:
        raise SystemExit("selftest: the script of the tree failed its own gate: "
                         + "; ".join(text for _, text in base))
    original = open(os.path.join(root, "sys", "conf", "newvers.sh"), encoding="utf-8").read()
    # Each mutant names the check that must catch it, so a mutant that fails for another
    # reason does not count.
    mutants = {
        "stage weight dropped":
            ("OSREV_PATCH * 10 + OSREV_STAGE", "OSREV_PATCH * 10", "check_good"),
        "major limit lifted":
            ('osrev_field major "$1" 20', 'osrev_field major "$1" 21', "check_rejected"),
        "leading zeros allowed":
            ('\t0?*) osrev_fail "OSR $1 \\"$2\\" has a leading zero" ;;\n', "",
             "check_rejected"),
        "release stage below its labels":
            ("OSREV_STAGE=9\nelse", "OSREV_STAGE=0\nelse", "check_good"),
        "label ignored against OSRANK":
            ('"$OSREV_LABEL"=[1-8])', "*=[1-8])", "check_rejected"),
        "minor weight shared with major":
            ("OSREV_MINOR * 1000000", "OSREV_MINOR * 100000000", "check_good"),
        "static assertion on the packed value renamed":
            ('"OSREV packed value disagrees with its fields"', '"OSREV packed value is fine"',
             "check_edits"),
    }
    for name, (old, new, want) in mutants.items():
        if old not in original:
            raise SystemExit(f"selftest: the mutant {name!r} anchors on text the script lacks")
        with tempfile.TemporaryDirectory() as mutant_root:
            os.makedirs(os.path.join(mutant_root, "sys", "conf"))
            with open(os.path.join(mutant_root, "sys", "conf", "newvers.sh"), "w",
                      encoding="utf-8") as handle:
                handle.write(original.replace(old, new, 1))
            caught = {check for check, _ in verdict(mutant_root)}
            if want not in caught:
                raise SystemExit(f"selftest: the mutant {name!r} was not caught by {want}; "
                                 f"the checks that failed: {sorted(caught)}")
    print(f"check-osrev-generator selftest: ok (the tree's script and {len(mutants)} mutants)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else "")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(__file__), ".."))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    root = os.path.abspath(args.root)
    if args.selftest:
        selftest(root)
        return 0
    defects = verdict(root)
    for _, defect in defects:
        print(f"check-osrev-generator: {defect}", file=sys.stderr)
    if defects:
        return 1
    print(f"check-osrev-generator: {len(GOOD)} good versions, a {len(CYCLE)}-step cycle, "
          f"{len(REJECTED)} rejected sources and {len(EDITS)} edited vers.c files behave")
    return 0


if __name__ == "__main__":
    sys.exit(main())
