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

"""Prove the devname() contract rejects the three defects it names.

Each mutant of lib/libc/gen/devname.c restores one defect, builds with the
contract's shims under AddressSanitizer, and must fail with the diagnostic
of that defect: the entry name returned after closedir() frees the DIR, a
match that ignores the requested file type, and a path buffer too short for
the longest directory entry name.
"""

import pathlib
import shlex
import subprocess
import sys
import tempfile

COMMON = [
    "-std=c17",
    "-pedantic-errors",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wstrict-prototypes",
    "-Wold-style-definition",
    "-Wmissing-prototypes",
    "-fsanitize=address,undefined",
    "-fno-sanitize-recover=all",
    "-fno-omit-frame-pointer",
]
RENAMES = [
    "-Ddevname=db_devname",
    "-Dopendir=test_opendir",
    "-Dreaddir=test_readdir",
    "-Dclosedir=test_closedir",
    "-Dstat=test_stat",
    "-Dperror=test_perror",
]
MUTANTS = (
    (
        "use-after-close",
        "\t\t\treturn (name);",
        "\t\t\treturn (entry->d_name);",
        "heap-use-after-free",
    ),
    (
        "any-type",
        "\t\tif ((st.st_mode & S_IFMT) != (mode_t)type)\n\t\t\tcontinue;\n",
        "\t\t(void)type;\n",
        "a block device answers for a character device",
    ),
    (
        "short-path",
        "static char devpath[DEVPREFIXLEN + MAXNAMLEN + 1];",
        "static char devpath[40];",
        "global-buffer-overflow",
    ),
)


def fail(message):
    raise SystemExit(f"devname mutants: {message}")


def run(command):
    subprocess.run(command, check=True)


def build(root, compiler, contracts, directory, name, source_text):
    source = directory / f"devname_{name}.c"
    source.write_text(source_text, encoding="utf-8")
    port = ["-nostdinc", "-isystem", str(root / "include"), *RENAMES]
    objects = []
    for unit, label in ((source, "devname"), (contracts / "devname_shim.c", "shim")):
        output = directory / f"{name}_{label}.o"
        run(compiler + COMMON + port + ["-I", str(contracts), "-c", str(unit), "-o", str(output)])
        objects.append(str(output))
    executable = directory / f"devname_{name}"
    run(
        compiler
        + COMMON
        + ["-I", str(contracts), str(contracts / "devname_contract_test.c")]
        + objects
        + ["-o", str(executable)]
    )
    return subprocess.run([str(executable)], capture_output=True, text=True, check=False)


def main():
    if len(sys.argv) != 4:
        fail("usage: devname_contract_mutate.py ROOT CC CONTRACTS_DIR")
    root = pathlib.Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    contracts = pathlib.Path(sys.argv[3]).resolve()
    source = (root / "lib/libc/gen/devname.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="devname-mutants-") as temporary:
        directory = pathlib.Path(temporary)
        baseline = build(root, compiler, contracts, directory, "baseline", source)
        if baseline.returncode != 0:
            fail(f"the unmutated source fails:\n{baseline.stderr}")
        for name, anchor, replacement, diagnostic in MUTANTS:
            if source.count(anchor) != 1:
                fail(f"{name}: the mutation anchor occurs {source.count(anchor)} times")
            result = build(
                root,
                compiler,
                contracts,
                directory,
                name,
                source.replace(anchor, replacement),
            )
            if result.returncode == 0:
                fail(f"{name}: the mutant passed the contract")
            if diagnostic not in result.stderr:
                fail(f'{name}: the mutant failed without "{diagnostic}":\n{result.stderr}')
            print(f"devname mutant {name}: failed with {diagnostic}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
