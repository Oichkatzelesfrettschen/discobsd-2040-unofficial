"""Relink existing kernels privately; measure flags and dispatch encodability.

No build products or production flags change. Compile/configure every selected
kernel first. Results describe linked artifacts, not execution or durability.
"""

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    "control": [],
    "sort-alignment": ["--sort-section=alignment"],
    "sort-common": ["--sort-common=descending"],
    "relax": ["--relax"],
    "no-relax": ["--no-relax"],
    "link-O2": ["-O2"],
    "combined": ["--sort-section=alignment", "--sort-common=descending", "--relax"],
}


def command(args, directory):
    environment = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS")}
    result = subprocess.run(args, cwd=directory, env=environment, text=True,
                            capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {args!r}\n"
                           + result.stdout + result.stderr)
    return result.stdout.strip()


def make_value(make, directory, name):
    return shlex.split(command([make, "-V", "${" + name + "}"], directory))


def encode_handlers(pointers, functions, anchor=0x10000100):
    if len(pointers) != 163 or any(p not in functions or not p & 1 for p in pointers):
        raise AssertionError("dispatch table contains a missing/non-Thumb handler")
    encoded = [(p - 1 - anchor) // 2 for p in pointers]
    if any(not 0 <= value <= 0xffff for value in encoded):
        raise AssertionError("dispatch halfword offset exceeds representable range")
    if any(anchor + value * 2 + 1 != pointer
           for value, pointer in zip(encoded, pointers)):
        raise AssertionError("dispatch address round trip differs")
    return encoded


def calibrate():
    valid = [0x10000101] * 163
    if encode_handlers(valid, set(valid)) != [0] * 163:
        raise AssertionError("valid dispatch representation rejected")
    for bad in ([0x10000100] * 163, [0x20000001] * 163, valid[:-1]):
        try:
            encode_handlers(bad, set(bad))
        except AssertionError:
            continue
        raise AssertionError("invalid dispatch representation accepted")


def object_footprint(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        sections = list(elf.iter_sections())
        code = b"".join(s.data() for s in sections if s["sh_flags"] & 4)
        return {"code_sha256": hashlib.sha256(code).hexdigest(),
                "text": sum(s["sh_size"] for s in sections if s["sh_flags"] & 4),
                "data": sum(s["sh_size"] for s in sections
                            if s["sh_flags"] & 3 == 3 and s["sh_type"] != "SHT_NOBITS"),
                "bss": sum(s["sh_size"] for s in sections
                           if s["sh_flags"] & 3 == 3 and s["sh_type"] == "SHT_NOBITS")}


def compare_interfaces(make, directory, reference, temporary):
    compiler = make_value(make, directory, "CC")
    flags = sum((make_value(make, directory, key) for key in ("CFLAGS", "INCLUDES", "PARAM")), [])
    measured = {}
    for unit in ("kern_proc", "kern_resource", "tty_subr", "kern_time"):
        relative = "sys/kern/" + unit + ".c"
        source = temporary / (unit + ".c")
        source.write_text(command(["git", "show", reference + ":" + relative], ROOT) + "\n")
        measured[unit] = {}
        for label, path in (("before", source), ("after", ROOT / relative)):
            output = temporary / (unit + "-" + label + ".o")
            command(compiler + flags + ["-DKERNEL", "-c", str(path), "-o", str(output)],
                    directory)
            measured[unit][label] = object_footprint(output)
    return measured


def hot_clock_object(make, directory, temporary):
    """Measure one bank without changing shared declarations or consumers.

    Assembly aliases preserve the existing symbol interface in this private
    prototype. Production admission still needs a reviewed ownership boundary
    and behavior evidence; aliases are not an automatic migration design.
    """
    text = (ROOT / "sys/kern/kern_clock.c").read_text()
    definitions = (
        "long    monosec;        /* monotonic clock: seconds since boot */",
        "u_short monotick;    /* monotonic clock: tick within the second */",
        "long    realsec = LONG_MAX;     /* earliest ITIMER_REAL deadline, or earlier */",
        "u_short realtick;",
    )
    bank = """
struct clock_bank_probe {
    long seconds, deadline;
    u_short tick, deadline_tick;
};
struct clock_bank_probe clock_bank_probe = { 0, LONG_MAX, 0, 0 };
_Static_assert(sizeof(struct clock_bank_probe) == 12, "bank size");
_Static_assert(_Alignof(struct clock_bank_probe) == 4, "bank alignment");
_Static_assert(__builtin_offsetof(struct clock_bank_probe, tick) == 8, "tick offset");
_Static_assert(__builtin_offsetof(struct clock_bank_probe, deadline_tick) == 10, "deadline offset");
__asm__(".global monosec, realsec, monotick, realtick\\n"
        ".set monosec, clock_bank_probe\\n"
        ".set realsec, clock_bank_probe+4\\n"
        ".set monotick, clock_bank_probe+8\\n"
        ".set realtick, clock_bank_probe+10\\n");
#define monosec clock_bank_probe.seconds
#define realsec clock_bank_probe.deadline
#define monotick clock_bank_probe.tick
#define realtick clock_bank_probe.deadline_tick
"""
    for index, definition in enumerate(definitions):
        if text.count(definition) != 1:
            raise AssertionError("clock-bank definition anchor differs")
        text = text.replace(definition, bank if index == 0 else "")
    source = temporary / "clock-bank.c"
    source.write_text(text)
    output = temporary / "clock-bank.o"
    compiler = make_value(make, directory, "CC")
    flags = sum((make_value(make, directory, key) for key in ("CFLAGS", "INCLUDES", "PARAM")), [])
    command(compiler + flags + ["-DKERNEL", "-c", str(source), "-o", str(output)], directory)
    return output


def verify_layout(make, directory, output):
    swapram = (["--swapram-object", "swapram.o"]
               if (directory / "swapram.o").is_file() else [])
    checks = (
        ["sh", str(ROOT / "tools/verify_rp2040_flash_swap_link.sh"),
         "--elf", str(output), "--flash-object", "flash.o", "--vm-swap-object", "vm_swap.o",
         "--exec-object", "exec_subr.o", "--swap-object", "swap.o"] + swapram,
        ["sh", str(ROOT / "tools/verify_rp2040_swapram_link.sh"),
         "--elf", str(output), "--object", "vm_swap.o", "--exec-object", "exec_hsaout.o",
         "--sysctl-object", "kern_sysctl.o", "--param",
         " ".join(make_value(make, directory, "PARAM"))],
        [os.environ["PYTHON"], str(ROOT / "tools/verify_rp2040_divider_ownership.py"),
         "--elf", str(output), "--dependency-directory", str(directory / ".deps")],
    )
    for check in checks:
        command(check, directory)


def larger_swapram(make, directory, temporary, linker, flags, objects, libraries):
    parameters = make_value(make, directory, "PARAM")
    if "-DSWAPRAM" not in parameters:
        return {"state": "not configured; no pool added"}
    compiler = make_value(make, directory, "CC")
    compile_flags = sum((make_value(make, directory, key)
                         for key in ("CFLAGS", "INCLUDES", "PARAM")), [])
    compile_flags = [flag for flag in compile_flags if not flag.startswith("-DSWAPRAM_KB=")]
    output = temporary / "swapram-32.o"
    command(compiler + compile_flags + ["-DSWAPRAM_KB=32", "-DKERNEL", "-c",
            str(ROOT / "sys/arch/rp2040/rp2040/swapram.c"), "-o", str(output)], directory)
    replaced = [str(output) if obj == "swapram.o" else obj for obj in objects]
    if replaced == objects:
        raise AssertionError("configured SwapRAM object absent")
    elf = temporary / "swapram-32.elf"
    try:
        command(linker + flags + replaced + ["-o", str(elf)] + libraries, directory)
    except RuntimeError as error:
        if "kernel RAM must keep 4,096 bytes free" not in str(error):
            raise
        return {"state": "rejected by production exchange-reserve assertion"}
    footprint, _ = inspect(elf)
    return {"state": "linked; runtime traffic unmeasured", "footprint": footprint}


def inspect(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s for s in elf.get_section_by_name(".symtab").iter_symbols()}
        loads = [segment for segment in elf.iter_segments()
                 if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]
                 and 0x10000000 <= segment["p_paddr"] < 0x10020000]
        footprint = {
            "flash_span": max(s["p_paddr"] + s["p_filesz"] for s in loads) - 0x10000000,
            "flash_payload": sum(s["p_filesz"] for s in loads),
            "data": elf.get_section_by_name(".data")["sh_size"],
            "bss": elf.get_section_by_name(".bss")["sh_size"],
            "swapram_pool": (elf.get_section_by_name(".swapram_pool")["sh_size"]
                             if elf.get_section_by_name(".swapram_pool") else 0),
            "post_exchange_margin": 0x2003e800 - 4096
                                    - symbols["__kernel_ram_used_end"]["st_value"],
        }
        table = symbols["syscall_handlers"]
        section = elf.get_section(table["st_shndx"])
        offset = table["st_value"] - section["sh_addr"]
        raw = section.data()[offset:offset + table["st_size"]]
        pointers = [int.from_bytes(raw[i:i + 4], "little") for i in range(0, len(raw), 4)]
        functions = {s["st_value"] for s in symbols.values()
                     if s["st_info"]["type"] == "STT_FUNC"}
        encoded = encode_handlers(pointers, functions)
        return footprint, {"slots": len(pointers), "largest_halfword_offset": max(encoded),
                           "gross_table_saving": len(raw) // 2}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="bmake")
    parser.add_argument("--board", action="append", required=True)
    parser.add_argument("--compare-ref")
    parser.add_argument("--hot-clock-bank", action="store_true")
    parser.add_argument("--swapram-32", action="store_true")
    parser.add_argument("--verify-layout", action="store_true",
                        help="verify control/sorted flash, SwapRAM and divider contracts")
    args = parser.parse_args()
    calibrate()
    result = {"source": command(["git", "rev-parse", "HEAD"], ROOT),
              "dirty": bool(command(["git", "status", "--porcelain"], ROOT)),
              "diff_sha256": hashlib.sha256(
                  subprocess.check_output(["git", "diff", "HEAD", "--binary"],
                                          cwd=ROOT)).hexdigest(),
              "variants": VARIANTS, "boards": {}}
    for board in args.board:
        if board not in ("PICO", "PICO_UART", "PICOCALC", "PICOCALC_PSRAM", "PICO_DIAG"):
            parser.error("unknown RP2040 board")
        directory = ROOT / "sys/arch/rp2040/compile" / board
        linker = make_value(args.make, directory, "LD")
        flags = [flag for flag in make_value(args.make, directory, "LDFLAGS")
                 if not flag.startswith("-Map=")]
        objects = make_value(args.make, directory, "SYSTEM_OBJ") + ["vers.o"]
        libraries = make_value(args.make, directory, "LDADD") + ["swapunix.o"]
        result["linker"] = command(linker + ["--version"], directory).splitlines()[0]
        baseline, _ = inspect(directory / "unix.elf")
        measured = {}
        with tempfile.TemporaryDirectory(prefix="discobsd-link-probe-") as temporary:
            if args.swapram_32:
                measured["swapram-32"] = larger_swapram(
                    args.make, directory, Path(temporary), linker, flags, objects, libraries)
            if args.compare_ref:
                measured["interfaces"] = compare_interfaces(
                    args.make, directory, args.compare_ref, Path(temporary))
            if args.hot_clock_bank:
                candidate = hot_clock_object(args.make, directory, Path(temporary))
                output = Path(temporary) / "hot-clock-bank.elf"
                replaced = [str(candidate) if obj == "kern_clock.o" else obj for obj in objects]
                if replaced == objects:
                    raise AssertionError("clock object absent from link closure")
                command(linker + flags + replaced + ["-o", str(output)] + libraries, directory)
                footprint, dispatch = inspect(output)
                measured["hot-clock-bank"] = {
                    "footprint": footprint,
                    "delta": {k: footprint[k] - baseline[k] for k in baseline},
                    "dispatch": dispatch,
                    "object_before": object_footprint(directory / "kern_clock.o"),
                    "object_after": object_footprint(candidate),
                }
            for name, extra in VARIANTS.items():
                output = Path(temporary) / (name + ".elf")
                command(linker + flags + extra + objects + ["-o", str(output)] + libraries,
                        directory)
                footprint, dispatch = inspect(output)
                if name == "control" and footprint != baseline:
                    raise AssertionError(f"{board}: private control differs from built kernel")
                measured[name] = {"footprint": footprint,
                                  "delta": {k: footprint[k] - baseline[k] for k in baseline},
                                  "dispatch": dispatch}
                if args.verify_layout and name in ("control", "sort-alignment"):
                    verify_layout(args.make, directory, output)
                    measured[name]["layout_checks"] = "flash, SwapRAM, divider passed"
        result["boards"][board] = measured
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
