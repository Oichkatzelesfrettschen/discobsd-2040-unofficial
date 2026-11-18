"""Build and compare private RP2040 halfword-dispatch kernels."""

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
import io
import shlex
import struct
import subprocess
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from check_kernel_headroom import flash_image_end
from check_rp2040_execsw import offset_for, symbol
from check_rp2040_syscalls import check_data, selected
from elftools.elf.elffile import ELFFile
from gen_kernel_metadata import parse_syscalls
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M0

ROOT = Path(__file__).resolve().parents[1]
COMPILE = ROOT / "sys/arch/rp2040/compile"
SCRIPT = ROOT / "sys/arch/rp2040/conf/kern.ldscript"
SYSCALL = ROOT / "sys/arch/rp2040/rp2040/syscall.c"
CONFIGS = ("PICO", "PICO_DIAG", "PICO_UART", "PICOCALC", "PICOCALC_LEAN", "PICOCALC_LEAN_DIAG")
FLASH = 0x10000000
TEXT = FLASH + 0x100
STACK = 0x20000000
DECODER = """\
\t\textern const uint16_t syscall_handler_offsets[];
\t\textern const char __syscall_thumb_anchor[];
\t\tuintptr_t address = (uintptr_t)__syscall_thumb_anchor +
\t\t    ((uintptr_t)syscall_handler_offsets[dispatch_code] << 1);
\t\t((syscall_handler_t)address)();\t/* Make syscall. */
"""


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f"expected one source anchor {before!r}")
    return text.replace(before, after)


def build_vars(directory):
    names = ("CC", "CFLAGS", "INCLUDES", "CPPFLAGS", "PARAM", "LD", "LDFLAGS",
             "SYSTEM_OBJ", "LDADD", "OBJCOPY")
    options = [arg for name in names for arg in ("-v", name)]
    output = subprocess.check_output(["bmake", *options], cwd=directory, text=True)
    return dict(zip(names, map(shlex.split, output.splitlines()), strict=True))


def linker_script(handlers):
    roots = sorted(set(handlers))
    assertions = [
        f'ASSERT((ABSOLUTE({name}) & 1) == 0 && '
        f'ABSOLUTE({name}) >= __syscall_text_anchor && '
        f'ABSOLUTE({name}) - __syscall_text_anchor <= 0x1fffe, '
        f'"invalid compact syscall target: {name}");'
        for name in roots
    ]
    offsets = [f"SHORT((ABSOLUTE({name}) - __syscall_text_anchor) >> 1);" for name in handlers]
    table = "\n\t\t".join((
        "__syscall_text_anchor = ABSOLUTE(ORIGIN(FLASH));",
        "__syscall_thumb_anchor = ABSOLUTE(__syscall_text_anchor + 1);",
        ". = ALIGN(2);",
        "syscall_handler_offsets = .;",
        *assertions,
        *offsets,
        "syscall_handler_offsets_end = .;",
    ))
    return "EXTERN(" + " ".join(roots) + ")\n" + replace_once(
        SCRIPT.read_text(), "*(.rodata.syscall_handlers)", table,
    )


def link(directory, out, variables, handlers):
    source = out / "syscall.c"
    source.write_text(replace_once(
        SYSCALL.read_text(), "\t\tsyscall_handlers[dispatch_code]();\t/* Make syscall. */\n",
        DECODER,
    ))
    obj = out / "syscall.o"
    subprocess.run([
        *variables["CC"], *variables["CFLAGS"], *variables["CPPFLAGS"], *variables["INCLUDES"],
        *variables["PARAM"], "-DKERNEL", "-c", str(source), "-o", str(obj),
    ], cwd=directory, check=True)
    script = out / "kern.ldscript"
    script.write_text(linker_script(handlers))
    flags = [
        f"-Map={out / 'unix.map'}" if flag.startswith("-Map=") else
        str(script) if flag.endswith("/kern.ldscript") else flag
        for flag in variables["LDFLAGS"]
    ]
    objects = [str(obj) if path == "syscall.o" else path for path in variables["SYSTEM_OBJ"]]
    elf = out / "unix.elf"
    subprocess.run([
        *variables["LD"], *flags, *objects, "swapunix.o", "vers.o",
        "-o", str(elf), *variables["LDADD"],
    ], cwd=directory, check=True)
    return elf


def read_values(elf, data, address, count, code):
    size = struct.calcsize(code) * count
    offset = offset_for(elf, data, address, size)
    return struct.unpack_from(f"<{count}{code}", data, offset)


def check_table(elf, data, wanted):
    table = elf.get_section_by_name(".symtab")
    start = symbol(table, "syscall_handler_offsets", "STT_NOTYPE")
    end = symbol(table, "syscall_handler_offsets_end", "STT_NOTYPE")
    anchor = symbol(table, "__syscall_text_anchor", "STT_NOTYPE")
    thumb_anchor = symbol(table, "__syscall_thumb_anchor", "STT_NOTYPE")
    if start % 2 or end - start != 2 * len(wanted) or anchor != TEXT or thumb_anchor != anchor + 1:
        raise ValueError("compact table extent or text anchor differs")
    if table.get_symbol_by_name("syscall_handlers"):
        raise ValueError("compact image still carries the full pointer table")
    hot_start = symbol(table, "__hot_start", "STT_NOTYPE")
    hot_end = symbol(table, "__hot_end", "STT_NOTYPE")
    if hot_start % 8 or not hot_start <= start < end <= hot_end <= hot_start + 8192:
        raise ValueError("compact table escapes the aligned hot block")
    count_address = symbol(table, "nsysent", "STT_OBJECT", 4)
    if read_values(elf, data, count_address, 1, "I") != (len(wanted),):
        raise ValueError("compact syscall count differs")
    counts = symbol(table, "syscall_nargs", "STT_OBJECT", len(wanted))
    offsets = read_values(elf, data, start, len(wanted), "H")
    for index, (off, argc, (handler, expected_argc)) in enumerate(zip(
        offsets, read_values(elf, data, counts, len(wanted), "B"), wanted, strict=True,
    )):
        target = (anchor + (off << 1)) | 1
        if target != symbol(table, handler, "STT_FUNC") or argc != expected_argc:
            raise ValueError(f"compact syscall {index} differs from {handler}")
        offset_for(elf, data, target & ~1, 2, executable=True)
    return start


def measure(elf, data, handlers, mutant=None):
    table = elf.get_section_by_name(".symtab")
    entry = symbol(table, "syscall", "STT_FUNC") & ~1
    setjmp = symbol(table, "setjmp", "STT_FUNC") & ~1
    length = table.get_symbol_by_name("syscall")[0]["st_size"]
    start = offset_for(elf, data, entry, length, executable=True)
    decoder = Cs(CS_ARCH_ARM, CS_MODE_MCLASS | CS_MODE_THUMB)
    instructions = list(decoder.disasm(data[start:start + length], entry))
    (resume,) = tuple(
        item.address + item.size for item in instructions
        if item.mnemonic == "bl" and item.op_str == f"#{setjmp:#x}"
    )
    # Start at the successful setjmp return. Supply the bounded selector in
    # the stack spill slots, and stop when BLX reaches the selected handler.
    jump = next(
        item.address for item in instructions if item.address >= resume and item.mnemonic == "blx"
    )
    cpu = Uc(UC_ARCH_ARM, UC_MODE_MCLASS | UC_MODE_THUMB)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
    cpu.mem_map(FLASH, 0x20000)
    cpu.mem_map(STACK, 0x1000)
    for segment in elf.iter_segments():
        if segment["p_type"] == "PT_LOAD" and FLASH <= segment["p_vaddr"] < FLASH + 0x20000:
            cpu.mem_write(segment["p_vaddr"], segment.data())
    if mutant:
        address, value = mutant
        cpu.mem_write(address, value)
    counts = set()
    path_count = 0
    jumped = False

    def count_instruction(uc, address, _size, _user_data):
        nonlocal path_count, jumped
        if jumped:
            uc.emu_stop()
        else:
            path_count += 1
            jumped = address == jump

    cpu.hook_add(UC_HOOK_CODE, count_instruction)
    for index, handler in enumerate(handlers):
        path_count = 0
        jumped = False
        cpu.mem_write(STACK, struct.pack("<64I", *([index] * 64)))
        cpu.reg_write(UC_ARM_REG_R0, 0)
        cpu.reg_write(UC_ARM_REG_SP, STACK)
        cpu.emu_start(resume | 1, 0, count=32)
        if not jumped or cpu.reg_read(UC_ARM_REG_PC) != symbol(table, handler, "STT_FUNC") & ~1:
            raise ValueError(f"executed syscall {index} differs from {handler}")
        counts.add(path_count)
    return sorted(counts)


def experiment(config, out, rows):
    directory = COMPILE / config
    out.mkdir()
    variables = build_vars(directory)
    defines = {p.removeprefix("-D").partition("=")[0] for p in variables["PARAM"]}
    features = {"inet": "INET" in defines, "glob": "GLOB_ENABLED" in defines}
    wanted = [selected(row, **features) for row in rows]
    handlers = [name for name, _ in wanted]
    original = (directory / "unix.elf").read_bytes()
    check_data(original, rows, systrace="SYSTRACE" in defines, **features)
    compact_path = link(directory, out, variables, handlers)
    compact = compact_path.read_bytes()
    old, new = (ELFFile(io.BytesIO(data)) for data in (original, compact))
    table_address = check_table(new, compact, wanted)

    baseline_steps = measure(old, original, handlers)
    compact_steps = measure(new, compact, handlers)
    first_offset = read_values(new, compact, table_address, 1, "H")[0]
    mutation = (table_address + 2, struct.pack("<H", first_offset))
    try:
        measure(new, compact, handlers[:2], mutant=mutation)
    except ValueError as error:
        if str(error) != f"executed syscall 1 differs from {handlers[1]}":
            raise
    else:
        raise ValueError("wrong-target mutation passed")

    # Image identity covers the packed load span; stamp it before extracting a boot image.
    subprocess.run([ROOT / "tools/bin/rp2040/kernsum", compact_path], check=True)
    subprocess.run([
        *variables["OBJCOPY"], "-O", "binary", str(compact_path), str(out / "unix.bin"),
    ], cwd=directory, check=True)
    spans = [flash_image_end(data, TEXT) - TEXT for data in (original, compact)]
    ram = [
        [image.get_section_by_name(section)["sh_size"] for section in (".data", ".bss")]
        for image in (old, new)
    ]
    print(f"{config}: flash span {spans[0]} -> {spans[1]} ({spans[1] - spans[0]:+d} B), "
          f"data/BSS {ram[0]} -> {ram[1]}, dispatch instructions "
          f"{baseline_steps} -> {compact_steps}; {len(handlers)} targets, bad target rejected",
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new directory for private images")
    parser.add_argument("configs", nargs="*", metavar="CONFIG")
    args = parser.parse_args()
    unknown = set(args.configs).difference(CONFIGS)
    if unknown:
        parser.error("unknown configuration " + ", ".join(sorted(unknown)))
    output = args.output.resolve()
    output.mkdir()
    rows = parse_syscalls(ROOT / "sys/kern/syscalls.master")
    for config in args.configs or CONFIGS:
        experiment(config, output / config, rows)


if __name__ == "__main__":
    main()
