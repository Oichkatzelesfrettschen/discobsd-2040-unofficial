"""Measure the flash and SRAM ledger of linked RP2040 kernels from their ELF files."""

# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

PREFIX = os.environ.get("GCCPREFIX", "arm-none-eabi")
KERNEL_FLASH_START = 0x10000100
SRAM_KERNEL_START = 0x20030000
COLUMNS = (
    "kernel",
    "flash_span",
    "flash_gap",
    "ram_used",
    "ram_gap",
    "pool",
    "scratch",
    "usb_ring",
    "atlas",
    "exchange_addr",
    "exchange_bank",
)
LOAD = re.compile(r"LOAD\s+0x\w+ 0x(\w+) 0x(\w+) 0x(\w+)")


def tool(name, *arguments):
    """Run one binutils program and return its standard output."""
    result = subprocess.run(
        [f"{PREFIX}-{name}", *arguments], capture_output=True, text=True, check=False
    )
    if result.returncode:
        raise SystemExit(f"{PREFIX}-{name} {' '.join(arguments)}: {result.stderr}")
    return result.stdout


def symbols(elf):
    """Map each symbol name to (address, size); size is 0 when nm omits it."""
    table = {}
    for line in tool("nm", "-S", str(elf)).splitlines():
        fields = line.split()
        if len(fields) == 4:
            table[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
        elif len(fields) == 3:
            table[fields[2]] = (int(fields[0], 16), 0)
    return table


def flash_end(elf):
    """Return the end of the flash load image: the highest LMA plus file size."""
    end = 0
    for match in LOAD.finditer(tool("readelf", "-lW", str(elf))):
        physical, file_size = int(match.group(2), 16), int(match.group(3), 16)
        if file_size and physical < SRAM_KERNEL_START:
            end = max(end, physical + file_size)
    if end == 0:
        raise SystemExit(f"{elf}: no flash load segment")
    return end


def bank(address):
    """Name the RP2040 SRAM region holding address."""
    if 0x20040000 <= address < 0x20041000:
        return "SRAM4"
    if 0x20041000 <= address < 0x20042000:
        return "SRAM5"
    if SRAM_KERNEL_START <= address < 0x2003E800:
        return "kernel-ram"
    return "other"


def measure(elf):
    """Return one ledger row for a linked kernel."""
    table = symbols(elf)
    required = ("__kernel_flash_end", "__kernel_ram_used_end", "__kernel_data_end")
    missing = [name for name in required if name not in table]
    if missing:
        raise SystemExit(f"{elf}: missing {', '.join(missing)}")

    def address(name):
        return table[name][0] if name in table else 0

    end = flash_end(elf)
    used_end = address("__kernel_ram_used_end")
    exchange = address("swap_exchange_sector")
    return {
        "kernel": elf.parent.name,
        "flash_span": end - KERNEL_FLASH_START,
        "flash_gap": address("__kernel_flash_end") - end,
        "ram_used": used_end - SRAM_KERNEL_START,
        "ram_gap": address("__kernel_data_end") - used_end,
        "pool": table.get("swapram_pool_mem", (0, 0))[1],
        "scratch": address("__scratch_end__") - address("__scratch_start__"),
        "usb_ring": table.get("usb_tx_ring", (0, 0))[1],
        "atlas": table.get("atlas", (0, 0))[1],
        "exchange_addr": f"0x{exchange:08x}",
        "exchange_bank": bank(exchange),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="+", type=Path)
    arguments = parser.parse_args()
    sys.stdout.write("\t".join(COLUMNS) + "\n")
    for elf in arguments.elf:
        row = measure(elf)
        sys.stdout.write("\t".join(str(row[column]) for column in COLUMNS) + "\n")


if __name__ == "__main__":
    main()
