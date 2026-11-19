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
"""Replay the full PicoCalc panel programs against the retained register traces."""

import argparse
import hashlib
import struct
from pathlib import Path

from kernel_emulator import BX_LR, KernelEmulator, KernelImage
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0

SPI1 = 0x40040000
SIO = 0xD0000000
FIXTURE = Path(__file__).with_name("panel-programs.tsv")


class Panel:
    """The PL022 accepts each byte immediately. Delay calls record milliseconds."""

    def __init__(self, image, patches=()):
        delay = image.function("mdelay").address
        self.machine = KernelEmulator(image, [(delay, BX_LR), *patches])
        cpu = self.machine.uc
        for address, size in (
            (0x4000C000, 0x4000),
            (0x40014000, 0x1000),
            (0x4001C000, 0x1000),
            (SPI1, 0x1000),
            (SIO, 0x1000),
        ):
            cpu.mem_map(address, size)
            cpu.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=address, end=address + size - 1)
        cpu.mem_write(0x4000C008, b"\xff" * 4)  # RESETS_DONE
        cpu.mem_write(SPI1 + 0x0C, (2).to_bytes(4, "little"))  # Tx FIFO has space
        cpu.hook_add(UC_HOOK_CODE, self.delay, begin=delay, end=delay)
        self.events = bytearray()

    def write(self, cpu, access, address, size, value, data):
        self.events.extend(struct.pack("<BII", size, address, value))

    def delay(self, cpu, address, size, data):
        self.events.extend(struct.pack("<BII", 0, 0, cpu.reg_read(UC_ARM_REG_R0)))

    def call(self, name, *args):
        return self.machine.call(name, *args, policy=None, budget=200000)

    def program(self, number):
        self.events.clear()
        if number is None:
            self.call("st7365p_init")
        else:
            result = self.call("st7365p_set", number)
            if result or self.call("st7365p_set_current") != number:
                raise ValueError(f"register set {number} returned {result} or selected another set")
        if not self.events:
            raise ValueError("empty register trace")
        return hashlib.sha256(self.events).hexdigest()

    def refused(self, number, current):
        self.events.clear()
        if self.call("st7365p_set", number) != 22 or self.events:
            raise ValueError(f"register set {number} requires EINVAL and zero writes")
        if self.call("st7365p_set_current") != current:
            raise ValueError(f"register set {number} changed the selected set")


def check_console_minors(image):
    (tty,) = image.objects("lcdttys")
    console_dev = 10 << 8
    uio = 0x20001000
    terminal = image.function("ttwrite").address
    graphics = image.function("lcdgfx_write").address
    patches = [(terminal, BX_LR), (graphics, BX_LR)]

    def routes(machine):
        calls = []

        def record(cpu, address, size, data):
            calls.append((address, cpu.reg_read(UC_ARM_REG_R0)))

        for entry in (terminal, graphics):
            machine.uc.hook_add(UC_HOOK_CODE, record, begin=entry, end=entry)
        for minor in (0, 1):
            machine.call("lcdwrite", console_dev | minor, uio, 0, policy=None, budget=64)
        return calls == [(terminal, tty.address), (graphics, uio)]

    machine = KernelEmulator(image, patches)
    if not routes(machine):
        raise ValueError("console or graphics write reaches the wrong object")
    for minor in range(2, 256):
        if machine.call("lcdopen", console_dev | minor, 0, 0, policy=None, budget=64) != 6:
            raise ValueError(f"unassigned console minor {minor} requires ENXIO")

    # A silent write returns to its caller before either dispatch.
    entry = image.function("lcdwrite").address
    mutant = KernelEmulator(image, [*patches, (entry, BX_LR)])
    if routes(mutant):
        raise ValueError("console routing accepted a silent write")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    image = KernelImage(args.elf)
    lines = FIXTURE.read_text(encoding="ascii").splitlines()
    check_console_minors(image)
    panel = Panel(image)
    for row, line in enumerate(lines):
        label, expected = line.split("\t")
        number = None if row == 0 else row - 1
        if label != ("boot" if number is None else str(number)):
            raise ValueError(f"unexpected trace label {label}")
        if panel.program(number) != expected:
            raise ValueError(f"{label}: register writes or delays differ from the reference")
    if len(lines) < 2:
        raise ValueError("missing boot or numbered register sets")
    panel.refused(-1, len(lines) - 2)
    panel.refused(len(lines) - 1, len(lines) - 2)

    # Replace the selected function with MOVS r0,#0; BX lr. Set zero
    # appears selected on reset and the return value still reports success.
    entry = image.function("st7365p_set").address
    mutant = Panel(image, [(entry, b"\x00\x20" + BX_LR)])
    try:
        mutant.program(0)
    except ValueError as error:
        if str(error) != "empty register trace":
            raise
    else:
        raise ValueError("set zero accepted a successful return without register traffic")
    print(
        f"PASS panel programs: boot, {len(lines) - 1} sets, two refusals, "
        "silent-set control and console minor routing"
    )


if __name__ == "__main__":
    main()
