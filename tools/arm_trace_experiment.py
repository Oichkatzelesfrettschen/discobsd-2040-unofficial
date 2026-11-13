"""Compile the production HSX decoder for M0+ and capture bounded Unicorn traces."""

import argparse
import csv
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from graph_layout_trace import read_symbols, read_trace, study
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
    UC_ARM_REG_SP,
)


def literal_stream(raw):
    bits = "".join("1" + f"{byte:08b}" for byte in raw)
    bits += "0" * (-len(bits) % 8)
    return bytes(int(bits[i : i + 8], 2) for i in range(0, len(bits), 8))


def emulate(elf, raw, trace_path, corrupt=False):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.mem_map(0x10000000, 2 * 1024 * 1024)
    mu.mem_map(0x20000000, 264 * 1024)
    with elf.open("rb") as stream:
        parsed = ELFFile(stream)
        for segment in parsed.iter_segments():
            if segment["p_type"] == "PT_LOAD":
                address = int(segment["p_vaddr"])
                if not 0x10000000 <= address < address + segment["p_memsz"] <= 0x10200000:
                    raise ValueError("unexpected ELF mapping")
                mu.mem_write(address, segment.data())
        entry = int(parsed.header["e_entry"])
    packed = literal_stream(raw)
    if corrupt:
        packed = bytes([packed[0] ^ 1]) + packed[1:]
    mu.mem_write(0x20000000, packed)
    sentinel = 0x101FFF00
    for reg, value in (
        (UC_ARM_REG_SP, 0x20040000),
        (UC_ARM_REG_LR, sentinel | 1),
        (UC_ARM_REG_R0, 0x20000000),
        (UC_ARM_REG_R1, len(packed)),
        (UC_ARM_REG_R2, 0x20002000),
        (UC_ARM_REG_R3, len(raw)),
    ):
        mu.reg_write(reg, value)
    rows = []

    def record(_machine, address, width, _user):
        rows.append((hex(address), width))

    mu.hook_add(UC_HOOK_CODE, record)
    mu.emu_start(entry | 1, sentinel, timeout=5_000_000, count=500_000)
    from unicorn.arm_const import UC_ARM_REG_PC

    if (
        mu.reg_read(UC_ARM_REG_PC) != sentinel
        or mu.reg_read(UC_ARM_REG_R0) != len(raw)
        or bytes(mu.mem_read(0x20002000, len(raw))) != raw
    ):
        raise ValueError("emulator budget exhausted or decoder output mismatch")
    with trace_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["pc", "width"])
        writer.writerows(rows)
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", choices=("zig", "gcc"), default="zig")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    fixture = root / "tools/graph-layout-example"
    args.output.mkdir(parents=True, exist_ok=False)
    if args.compiler == "zig":
        compiler = [sys.executable, "-m", "ziglang", "cc"]
        target = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_m0plus", "-Oz"]
        version = [sys.executable, "-m", "ziglang", "version"]
    else:
        compiler = ["arm-none-eabi-gcc"]
        target = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
        version = ["arm-none-eabi-gcc", "--version"]
    with tempfile.TemporaryDirectory(prefix="discobsd-arm-") as folder:
        build = Path(folder)
        (build / "machine").mkdir()
        shutil.copy(root / "sys/arch/rp2040/include/hsx_decoder.h", build / "machine/hsx_decoder.h")
        elf = args.output / "decoder.elf"
        command = (
            compiler
            + target
            + [
                "-Wall",
                "-Wextra",
                "-Werror",
                "-g",
                "-Wl,--entry=workload",
                "-ffreestanding",
                "-fno-builtin",
                "-ffunction-sections",
                "-nostdlib",
                "-Wl,-T," + str(fixture / "arm_workload.ld"),
                "-I" + str(build),
                str(fixture / "arm_workload.c"),
                str(root / "sys/arch/rp2040/rp2040/hsx_decoder.c"),
                "-o",
                str(elf),
            ]
        )
        subprocess.run(command, check=True, timeout=120)
    records = []
    with elf.open("rb") as stream:
        for symbol in ELFFile(stream).get_section_by_name(".symtab").iter_symbols():
            if symbol["st_info"]["type"] == "STT_FUNC" and symbol["st_size"]:
                records.append((int(symbol["st_value"]) & ~1, int(symbol["st_size"]), symbol.name))
    nm = args.output / "symbols.nm"
    nm.write_text(
        "".join(f"{address:08x} {size:08x} T {name}\n" for address, size, name in sorted(records))
    )
    training, heldout = args.output / "training.csv", args.output / "heldout.csv"
    counts = [
        emulate(elf, bytes(range(128)), training),
        emulate(elf, b"DiscoBSD tiny HSX trace\n" * 3, heldout),
    ]
    try:
        emulate(elf, bytes(range(16)), args.output / "mutant.csv", corrupt=True)
    except ValueError as error:
        if "output mismatch" not in str(error):
            raise
    else:
        raise ValueError("corrupt-input control was accepted")
    symbols = read_symbols(nm)
    report = study(
        symbols, read_trace(training, symbols), read_trace(heldout, symbols), 100, 1729, 8
    )
    report["evidence"] = (
        "compiled production decoder under Unicorn; not a board/cache timing measurement"
    )
    report["instructions"] = counts
    report["corrupt_input_rejected"] = True
    report["sha256"] = {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (
            fixture / "arm_workload.c",
            fixture / "arm_workload.ld",
            root / "sys/arch/rp2040/rp2040/hsx_decoder.c",
            root / "sys/arch/rp2040/include/hsx_decoder.h",
        )
    }
    report["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
    report["compiler"] = subprocess.check_output(version, text=True).splitlines()[0]
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"instructions": counts, "metrics": report["metrics"]}, indent=2))


if __name__ == "__main__":
    main()
