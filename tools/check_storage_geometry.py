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
# Hold the storage sizes that build files restate to the headers they come
# from. check-picocalc-sd-layout already proves both linker scripts against
# flash.h; this gate covers the restatements outside them:
#   - FS_KBYTES in distrib/rp2040/Makefile.inc must be the capacity a
#     flashimg built from the tested sources reports with -c, less the
#     partition table fsutil writes in front of the first partition
#     (create_partition_table() in tools/fsutil/fsutil.c, "offset = N"
#     sectors of 512 bytes);
#   - FLASH_FS_ADDR in Makefile.inc, where the flash image's UF2 is placed,
#     must be FLASH_XIP_BASE + FLASH_FS_OFFSET of the default flash.h arm;
#   - the size etc/rc.rp2040 gives mkfs for the PSRAM /tmp (/dev/psram1)
#     must be (PSRAM_BYTES - PSRAM_SWAP_BYTES) / 1024 of psram_xfer.h.
# The header values are compared by _Static_assert in a C translation unit
# compiled against the real headers, so a reading never parses an
# expression. The self-test mutates each restated value once and requires
# the verdict to reject it.
#
# usage: check_storage_geometry.py [--self-test] [--root ROOT] [--cc CC]

import argparse
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INPUTS = {
    "makefile": "distrib/rp2040/Makefile.inc",
    "rc": "etc/rc.rp2040",
    "fsutil": "tools/fsutil/fsutil.c",
}
CAPACITY = re.compile(r"region (\d+) kbytes, capacity (\d+) sectors of (\d+) bytes, (\d+) kbytes")


class Unreadable(Exception):
    """An input the gate cannot read; the verdict is an error, not a pass."""


def one(pattern, text, what, flags=re.M):
    found = re.findall(pattern, text, flags)
    if len(found) != 1:
        raise Unreadable(f"{what}: {len(found)} matches, expected 1")
    return found[0]


def flashimg_capacity(root, cc, directory):
    """Build flashimg from ROOT's sources and return its -c kbytes."""
    dhara = root / "sys/arch/rp2040/dhara"
    sources = [root / "tools/flashimg/flashimg.c"] + [
        dhara / f"{n}.c" for n in ("error", "journal", "map")
    ]
    binary = directory / "flashimg"
    command = [
        *cc,
        "-O",
        "-Wall",
        "-I",
        str(dhara),
        "-I",
        str(root / "sys/arch"),
        "-o",
        str(binary),
    ]
    built = subprocess.run(command + [str(s) for s in sources], capture_output=True, text=True)
    if built.returncode != 0:
        raise Unreadable(f"flashimg did not build: {built.stderr.strip()[:400]}")
    run = subprocess.run([str(binary), "-c"], capture_output=True, text=True)
    match = CAPACITY.search(run.stdout)
    if run.returncode != 0 or match is None:
        raise Unreadable(f"flashimg -c printed no capacity: {run.stdout.strip()[:200]}")
    return int(match.group(4))


def header_defects(root, cc, directory, address, tmp_kbytes):
    """Compile the header equalities; return each one the compiler rejects."""
    dev = root / "sys/arch/rp2040/dev"
    checks = {
        "FLASH_FS_ADDR": f"FLASH_XIP_BASE + FLASH_FS_OFFSET == {address}",
        "rc.rp2040 /dev/psram1 size": f"(PSRAM_BYTES - PSRAM_SWAP_BYTES) / 1024 == {tmp_kbytes}",
    }
    defects = []
    for name, condition in checks.items():
        unit = directory / "geometry.c"
        unit.write_text(
            f'#include "flash.h"\n#include "psram_xfer.h"\n_Static_assert({condition}, "{name}");\n'
        )
        result = subprocess.run(
            [*cc, "-std=c17", "-fsyntax-only", "-I", str(dev), str(unit)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            if (
                "static assertion failed" not in result.stderr
                and "static_assert" not in result.stderr
            ):
                raise Unreadable(
                    f"{name}: the headers did not compile: {result.stderr.strip()[:300]}"
                )
            defects.append(f"{name}: {condition} is false")
    return defects


def verdict(texts, capacity_kb, root, cc, directory):
    fs_kbytes = int(one(r"^FS_KBYTES=\s*(\d+)\s*$", texts["makefile"], "FS_KBYTES"))
    address = one(r"^FLASH_FS_ADDR=\s*(0x[0-9a-fA-F]+)\s*$", texts["makefile"], "FLASH_FS_ADDR")
    tmp_kbytes = int(one(r"mkfs -i \d+ /dev/psram1 (\d+)", texts["rc"], "rc.rp2040 psram1 mkfs"))
    body = one(
        r"^void create_partition_table \(.*?^}",
        texts["fsutil"],
        "create_partition_table",
        re.M | re.S,
    )
    offset = int(one(r"^\s*offset = (\d+);", body, "partition table offset"))
    if offset * 512 % 1024:
        raise Unreadable(f"partition table of {offset} sectors is not whole kbytes")
    defects = []
    expected = capacity_kb - offset * 512 // 1024
    if fs_kbytes != expected:
        defects.append(
            f"FS_KBYTES is {fs_kbytes}; flashimg -c reports {capacity_kb} kbytes "
            f"and the partition table takes {offset} sectors, so it must be {expected}"
        )
    return defects + header_defects(root, cc, directory, address, tmp_kbytes)


def read_inputs(root):
    try:
        return {key: (root / path).read_text() for key, path in INPUTS.items()}
    except OSError as error:
        raise Unreadable(f"{error.filename}: {error.strerror}") from error


def self_test(root, cc, directory, capacity_kb):
    texts = read_inputs(root)
    mutations = [
        (
            "FS_KBYTES one above",
            "makefile",
            r"^(FS_KBYTES=\s*)(\d+)",
            lambda m: f"{m[1]}{int(m[2]) + 1}",
        ),
        (
            "FLASH_FS_ADDR one erase block up",
            "makefile",
            r"^(FLASH_FS_ADDR=\s*)(0x[0-9a-fA-F]+)",
            lambda m: f"{m[1]}{int(m[2], 16) + 8192:#x}",
        ),
        (
            "the /tmp mkfs size doubled",
            "rc",
            r"(mkfs -i \d+ /dev/psram1 )(\d+)",
            lambda m: f"{m[1]}{int(m[2]) * 2}",
        ),
        (
            "a partition table two sectors longer",
            "fsutil",
            r"^(\s*offset = )(\d+)(;)",
            lambda m: f"{m[1]}{int(m[2]) + 2}{m[3]}",
        ),
    ]
    failed = 0
    defects = verdict(texts, capacity_kb, root, cc, directory)
    print(
        f"self-test: the tested tree: {len(defects)} defects, expected 0: "
        f"{'ok' if not defects else 'FAIL'}"
    )
    failed += bool(defects)
    for label, key, pattern, change in mutations:
        mutated = dict(texts)
        mutated[key], count = re.subn(pattern, change, texts[key], count=1, flags=re.M)
        if count != 1:
            print(f"self-test: {label}: the mutation did not apply: FAIL")
            failed += 1
            continue
        found = verdict(mutated, capacity_kb, root, cc, directory)
        ok = len(found) == 1
        failed += not ok
        print(f"self-test: {label}: {len(found)} defects, expected 1: {'ok' if ok else 'FAIL'}")
    print(f"check_storage_geometry self-test: {'pass' if not failed else f'{failed} failed'}")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--cc", default="cc")
    arguments = parser.parse_args()
    cc = shlex.split(arguments.cc)
    root = arguments.root.resolve()
    try:
        with tempfile.TemporaryDirectory(prefix="storage-geometry-") as name:
            directory = Path(name)
            capacity_kb = flashimg_capacity(root, cc, directory)
            if arguments.self_test:
                return self_test(root, cc, directory, capacity_kb)
            defects = verdict(read_inputs(root), capacity_kb, root, cc, directory)
    except Unreadable as error:
        print(f"ERROR storage-geometry: {error}", file=sys.stderr)
        return 2
    for defect in defects:
        print(f"FAIL storage-geometry: {defect}")
    if defects:
        return 1
    print(
        f"PASS storage-geometry: FS_KBYTES, FLASH_FS_ADDR and the /tmp mkfs size match "
        f"flashimg -c ({capacity_kb} kbytes), flash.h and psram_xfer.h"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
