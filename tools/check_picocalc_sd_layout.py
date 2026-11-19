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
"""Check the flash-driver erase geometry against both PicoCalc link maps."""

import argparse
import os
import shlex
import subprocess
from pathlib import Path

from check_kernel_headroom import region_bounds

ROOT = Path(__file__).resolve().parents[1]
CONF = ROOT / "sys/arch/rp2040/conf"
REGIONS = ("BOOT2", "FLASH", "FSFLASH", "SWAPFLASH", "USERRAM", "RAM", "U0AREA", "UAREA", "SCRATCH")
GROWTH = 32 * 1024


def regions(source):
    return {name: region_bounds(source, name) for name in REGIONS}


def compare(base, sd):
    expected = base.copy()
    origin, size = base["FLASH"]
    fs, fs_size = base["FSFLASH"]
    if origin + size != fs or fs + fs_size != base["SWAPFLASH"][0]:
        raise ValueError("default flash regions are discontinuous")
    expected.update(FLASH=(origin, size + GROWTH), FSFLASH=(fs + GROWTH, fs_size - GROWTH))
    if sd != expected:
        raise ValueError("SD kernel must acquire exactly 32 KiB from the filesystem")


def check_driver(layout, flags, cc):
    start, size = layout["FSFLASH"]
    swap, swap_size = layout["SWAPFLASH"]
    source = f"""
#include "flash.h"
_Static_assert(FLASH_XIP_BASE + FLASH_FS_OFFSET == {start}, "filesystem start");
_Static_assert(FLASH_FS_BYTES == {size}, "filesystem size");
_Static_assert(FLASH_XIP_BASE + FLASH_SWAP_OFFSET == {swap}, "swap start");
_Static_assert(FLASH_SWAP_BYTES == {swap_size}, "swap size");
_Static_assert(FLASH_FS_BYTES % FLASH_ERASE_BYTES == 0, "whole Dhara blocks");
_Static_assert(FLASH_FS_OFFSET % FLASH_ERASE_BYTES == 0, "aligned Dhara blocks");
"""
    return subprocess.run(
        [
            *cc,
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsyntax-only",
            "-I",
            str(CONF.parent / "dev"),
            *flags,
            "-x",
            "c",
            "-",
        ],
        input=source,
        text=True,
        capture_output=True,
        check=False,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    cc = shlex.split(parser.parse_args().cc)
    base = regions((CONF / "RP2040.ld").read_text())
    sd = regions((CONF / "RP2040-sd.ld").read_text())
    compare(base, sd)
    for layout, flags in ((base, []), (sd, ["-DPICOCALC_SD"])):
        match = check_driver(layout, flags, cc)
        if match.returncode:
            raise ValueError(match.stderr)
    wrong_driver = check_driver(sd, [], cc)
    if wrong_driver.returncode == 0 or "filesystem start" not in wrong_driver.stderr:
        raise ValueError("standard driver accepted the expanded link map or failed elsewhere")
    print("PASS PicoCalc SD layout: 32768 bytes, both driver maps, mismatched driver rejected")


if __name__ == "__main__":
    main()
