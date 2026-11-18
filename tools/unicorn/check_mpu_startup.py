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
"""Exercise the kernel's MPU setup and diagnostic ABI on a Cortex-M0 model."""

import argparse
from pathlib import Path

from kernel_emulator import CallFailure, KernelEmulator, KernelImage
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC

# MPU.md, map table. Literal words keep this oracle independent of mpu.c.
GRANTS = [
    (0, 0x0600001B),
    (0x20000000, 0x03000021),
    (0x20020000, 0x0300001F),
    (0xD0000000, 0x1300F70F),
]
TYPE, CTRL, RNR, RBAR, RASR = range(0xD90, 0xDA4, 4)


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


def check_case(image, fault, regions, slot=None):
    machine = KernelEmulator(image)
    slots = [[0x20000000, 1] for _ in range(regions)]
    selected = 0
    control = 5 if regions else 0
    observed_disabled = False
    requests = []
    writes = []

    def read(cpu, offset, width, data):
        nonlocal observed_disabled
        require(width == 4, "MMIO read width")
        if offset == TYPE:
            return regions << 8
        if offset == CTRL:
            observed_disabled = control == 0
            return control
        if offset == RNR:
            return selected
        if offset == RBAR:
            value = slots[selected][0] | selected
            return value ^ 256 if fault == "base" and selected == slot else value
        require(offset == RASR, "unknown MMIO read")
        return slots[selected][1]

    def write(cpu, offset, width, value, data):
        nonlocal control, selected, observed_disabled
        require(width == 4, "MMIO write width")
        if offset == CTRL:
            observed_disabled = False
            requests.append(value)
            if fault == "stuck" or (fault == "rollback" and len(requests) == 3):
                return
            if value == 5 and fault in ("partial", "rollback"):
                control = 1
            elif value == 5 and fault == "enable":
                control = 0
            else:
                control = value
            return
        require(control == 0 and observed_disabled, "region write before verified disable")
        writes.append(offset)
        if offset == RBAR:
            n = value & 15
            require(value & 16 and n < regions, "invalid region selection")
            if fault != "selector" or n != slot:
                selected = n
                slots[n][0] = value & 0xFFFFFF00
        else:
            require(offset == RASR, "unknown MMIO write")
            if fault != "attributes" or selected != slot:
                slots[selected][1] = value

    machine.uc.mmio_map(0xE000E000, 0x1000, read, None, write, None)
    panic = image.function("panic").address
    machine.uc.hook_add(UC_HOOK_CODE, lambda cpu, *_: cpu.emu_stop(), begin=panic, end=panic)
    try:
        machine.call("mpu_init", policy=None, budget=10000)
    except CallFailure as error:
        if error.kind != "budget" or machine.uc.reg_read(UC_ARM_REG_PC) != panic:
            raise
        require(fault in ("stuck", "rollback"), "unexpected panic")
        require(requests == ([0] if fault == "stuck" else [0, 5, 0]), "panic sequence")
        return
    require(fault not in ("stuck", "rollback"), "returned after a failed disable")

    enough = 4 <= regions <= 16
    count = 4 if enough else 0
    if slot is not None and slot < 4:
        count -= 1
    enabled = enough and fault == "normal"
    expected_control = 5 if enabled else 0
    expected_requests = [0] if regions else []
    if enabled:
        expected_requests.append(5)
        require([tuple(s) for s in slots] == GRANTS + [(0, 0)] * (regions - 4), "map")
    elif fault in ("partial", "enable"):
        expected_requests.extend([5, 0])
    require(enough or not writes, "region writes with unsupported count")
    require(requests == expected_requests and control == expected_control, "control sequence")

    # Run the exported handler through its real copyout into the user window.
    name = machine.alloc(4, align=4)
    length = machine.alloc(4, align=4)
    dest = 0x20001000
    leaves = [int(enabled), expected_control, regions, 0, count]
    for leaf, expected in enumerate(leaves, 1):
        machine.write(name, leaf.to_bytes(4, "little"))
        machine.write(length, (4).to_bytes(4, "little"))
        machine.write(dest, b"xxxxx")
        rc = machine.call("mpu_sysctl", name, 1, dest, length, 0, 0, policy=None, budget=1000)
        require(rc == 0 and machine.word(length) == 4, f"sysctl leaf {leaf} status or length")
        require(
            machine.read(dest, 5) == expected.to_bytes(4, "little") + b"x", f"sysctl leaf {leaf}"
        )

    if fault == "normal" and regions in (0, 8):
        for start in (0, 1, 2047, 2048, 16383, 16384, 16385, 0x20000000, 0xD0000060, 0xFFFFFFFF):
            for length in (0, 1, 2, 16, 2048, 16383, 16384, 16385, 0xFFFFFFFF):
                expected = start <= 16384 and length <= 16384 - start
                actual = machine.call("mpu_user_rospan", start, length, policy=None, budget=1000)
                require(actual == expected, f"ROM span {start:#x}+{length:#x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, nargs="+")
    paths = parser.parse_args().elf
    cases = [("normal", n, None) for n in (0, 3, 4, 8, 16, 17, 255)]
    cases += [
        (fault, 8, n)
        for fault in ("attributes", "base", "selector")
        for n in range(8)
        if fault != "selector" or n != 0
    ]
    cases += [(fault, 8, None) for fault in ("partial", "enable", "rollback", "stuck")]
    for path in paths:
        image = KernelImage(path)
        for case in cases:
            try:
                check_case(image, *case)
            except (ValueError, CallFailure) as error:
                raise SystemExit(f"FAIL {path} {case}: {error}") from error
        print(f"PASS {path}: {len(cases)} startup cases and 180 ROM spans")


if __name__ == "__main__":
    main()
