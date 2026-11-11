"""Execute every generated dispatch slot as Cortex-M0 instructions."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from dispatch_contracts import selected_rows
from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_SP,
    UC_CPU_ARM_CORTEX_M0,
)

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import verify_rp2040_dispatch as image_check  # noqa: E402


def run(command, directory):
    return subprocess.run(command, cwd=directory, capture_output=True, text=True,
                          env=dict(os.environ, LC_ALL="C"), check=False)


def checked(command, directory):
    result = run(command, directory)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def harness(rows, *, compact, inet, glob, trace, omit_nosys=False, far_nosys=False):
    handlers = sorted({row[2] for row in rows} | {"nonet"})
    lines = ['#include <sys/syscall_dispatch.h>', 'static volatile unsigned int observed;',
             'static syscall_handler_t volatile selected;']
    for number, handler in enumerate(handlers, 1):
        if handler == "nosys" and omit_nosys:
            continue
        attribute = ('__attribute__((section(".ramfunc"))) '
                     if far_nosys and handler == "nosys" else '')
        lines.append(f'{attribute}void {handler}(void) {{ observed = {number}; }}')
    if trace:
        lines.extend([
            'static int names(unsigned int code, const char *right)',
            '{ unsigned int length; const char *left = syscall_name(code, &length);',
            '  while (length && *right && *left == *right) { --length; ++left; ++right; }',
            '  return length == 0 && *right == 0; }',
        ])
    lines.extend(['int main(void)', '{', f' if (nsysent != {len(rows)}) return 1;'])
    for index, (handler, count, name) in enumerate(selected_rows(rows, inet, glob)):
        identity = handlers.index(handler) + 1
        getter = f'syscall_handler_at({index})' if compact else f'syscall_handlers[{index}]'
        lines.extend([
            f' if (syscall_nargs[{index}] != {count}) return 2;',
            f' selected = {getter}; if (selected != {handler}) return 3;',
            ' observed = 0; selected();',
            f' if (observed != {identity}) return 4;',
        ])
        if trace:
            lines.append(f' if (!names({index}, "{name}")) return 5;')
    lines.extend([' return 0;', '}'])
    return '\n'.join(lines) + '\n'


def build(work, rows, *, compact=True, inet=False, glob=False, trace=False, gc=False,
          zero=False, header_change=None, script_change=None, omit_nosys=False,
          far_nosys=False, absolute_nosys=None, expected_link_error=None, compiler=None,
          sort=False):
    compiler = shutil.which(compiler or "arm-none-eabi-gcc")
    linker = shutil.which("arm-none-eabi-ld")
    if not compiler or not linker:
        raise RuntimeError("ARM GCC and ld are required")
    work.mkdir()
    (work / "sys").mkdir()
    header = (ROOT / "sys/sys/syscall_dispatch.h").read_text()
    if header_change:
        before, after = header_change
        if header.count(before) != 1:
            raise RuntimeError("decoder mutation anchor differs")
        header = header.replace(before, after)
    (work / "sys/syscall_dispatch.h").write_text(header)
    defines = []
    options = (("COMPACT_SYSCALL_DISPATCH", compact), ("INET", inet),
               ("GLOB_ENABLED", glob), ("SYSTRACE", trace))
    for name, enabled in options:
        if enabled:
            defines.append("-D" + name + ("=0" if zero else ""))
    flags = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
             "-Wstrict-prototypes", "-Wold-style-definition", "-Os", "-ffreestanding",
             "-fno-builtin", "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft",
             "-ffunction-sections", "-fdata-sections", "-nostdinc",
             "-I" + str(work), "-I" + str(ROOT / "sys"), *defines]
    if Path(compiler).name.startswith("clang"):
        flags.insert(0, "--target=arm-none-eabi")
    source = work / "harness.c"
    source.write_text(harness(rows, compact=compact, inet=inet, glob=glob, trace=trace,
                              omit_nosys=omit_nosys, far_nosys=far_nosys))
    objects = []
    sources = [source, ROOT / "sys/kern/init_sysent.c"]
    if trace:
        sources.append(ROOT / "sys/kern/syscalls.c")
    for source in sources:
        output = work / (source.stem + ".o")
        checked([compiler, *flags, "-c", str(source), "-o", str(output)], work)
        objects.append(str(output))
    if absolute_nosys is not None:
        source = work / "absolute.S"
        source.write_text(f".global nosys\n.set nosys, {absolute_nosys}\n")
        output = work / "absolute.o"
        checked([compiler, "-mcpu=cortex-m0plus", "-mthumb", "-c", str(source),
                 "-o", str(output)], work)
        objects.append(str(output))
    for kind in ("roots", "offsets"):
        output = work / f"syscall_{kind}.ld"
        checked([compiler, "-E", "-P", "-x", "c", *defines,
                 str(ROOT / f"sys/kern/syscall_{kind}.ld.S"), "-o", str(output)], work)
        if kind == "offsets" and script_change:
            before, after = script_change
            text = output.read_text()
            if before not in text:
                raise RuntimeError("link-script mutation anchor differs")
            output.write_text(text.replace(before, after, 1))
    image = work / "image.elf"
    link = [linker, "-e", "main", "--fatal-warnings", "--warn-rwx-segments",
            "-T", str(ROOT / "sys/arch/rp2040/conf/RP2040.ld"),
            "-T", str(ROOT / "sys/arch/rp2040/conf/kern.ldscript"),
            *objects, "-o", str(image)]
    if gc:
        link.append("--gc-sections")
    if sort:
        link.append("--sort-section=alignment")
    result = run(link, work)
    if expected_link_error:
        if result.returncode != 1 or expected_link_error not in result.stderr:
            raise RuntimeError("link rejection differs: " + result.stderr)
        return None, defines
    if result.returncode:
        raise RuntimeError(result.stderr)
    return image, defines


def execute(image):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
    cpu.mem_map(0x10000000, 0x20000)
    cpu.mem_map(0x20000000, 0x42000)
    with image.open("rb") as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
                cpu.mem_write(segment["p_vaddr"], segment.data())
    symbols = image_check.read_symbols(image)
    stop = 0x1001fff0
    cpu.reg_write(UC_ARM_REG_SP, 0x2003e800)
    cpu.reg_write(UC_ARM_REG_LR, stop | 1)
    cpu.emu_start(symbols["main"].address, stop, count=1_000_000)
    if cpu.reg_read(UC_ARM_REG_PC) != stop:
        raise RuntimeError("decoder fixture did not return within its instruction budget")
    return cpu.reg_read(UC_ARM_REG_R0)


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    rows = image_check.master_rows()
    with tempfile.TemporaryDirectory(prefix="discobsd-compact-dispatch-") as temporary:
        base = Path(temporary)
        unsupported = base / "unsupported.c"
        unsupported.write_text('#include <sys/syscall_dispatch.h>\n')
        result = run(["clang", "-std=c17", "-fno-builtin", "-DCOMPACT_SYSCALL_DISPATCH",
                      "-nostdinc", "-I" + str(ROOT / "sys"), "-fsyntax-only",
                      str(unsupported)], base)
        if result.returncode != 1 or "requires ARM Thumb" not in result.stderr:
            raise RuntimeError("unsupported architecture was not rejected")
        count = 0
        for compact in (False, True):
            for inet, glob, trace, gc, zero, sort in (
                (False, False, False, False, False, False),
                (True, False, True, False, False, False),
                (False, True, False, True, False, False),
                (True, True, True, True, False, False),
                (True, True, True, True, True, False),
                (True, True, True, True, False, True),
            ):
                image, defines = build(base / str(count), rows, compact=compact, inet=inet,
                                       glob=glob, trace=trace, gc=gc, zero=zero, sort=sort)
                image_check.validate(image_check.read_symbols(image), rows,
                                     {flag[2:].split("=")[0] for flag in defines})
                if execute(image) != 0:
                    raise RuntimeError("valid dispatch execution failed")
                count += 1
        image, defines = build(base / "clang", rows, compiler="clang", inet=True,
                               glob=True, trace=True, gc=True, zero=True)
        image_check.validate(image_check.read_symbols(image), rows,
                             {flag[2:].split("=")[0] for flag in defines})
        if execute(image) != 0:
            raise RuntimeError("Clang compact dispatch execution failed")
        count += 1
        for label, mutation in (("thumb-bit", ("+ 1U", "+ 0U")),
                                ("halfword-scale", ("<< 1", "<< 0"))):
            image, _ = build(base / label, rows, header_change=mutation)
            if execute(image) != 3:
                raise RuntimeError("decoder mutation did not fail pointer comparison")
        for label, arguments in (
            ("upper-range", {"far_nosys": True}),
            ("lower-range", {"omit_nosys": True, "absolute_nosys": 0x10000000}),
        ):
            build(base / label, rows, expected_link_error="outside flash range", **arguments)
        for label, mutation, diagnostic in (
            ("alignment", (". = ALIGN(2);", ". = ALIGN(2) + 1;"), "table must be even"),
            ("extent", ("SHORT((ABSOLUTE(nosys) - __syscall_text_anchor) >> 1);", ""),
             "table extent differs"),
        ):
            build(base / label, rows, script_change=mutation, expected_link_error=diagnostic)
        image, defines = build(base / "not-thumb", rows, omit_nosys=True,
                               absolute_nosys=0x10000100)
        try:
            image_check.validate(image_check.read_symbols(image), rows,
                                 {flag[2:] for flag in defines})
        except ValueError as error:
            if "non-Thumb" not in str(error):
                raise
        else:
            raise RuntimeError("non-Thumb target passed image admission")
    print(f"PASS compact dispatch: {count} Cortex-M0 executions, all {len(rows)} slots; "
          "two decoder, four linker, one Thumb-metadata and one architecture control rejected")


if __name__ == "__main__":
    main()
