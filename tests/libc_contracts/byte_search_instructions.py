"""Calibrate Cortex-M0+ byte-search instruction work independently of timing."""

import argparse
import hashlib
import io
import json
import shlex
import subprocess
import tempfile
from pathlib import Path

try:
    import elftools
    import unicorn
    from elftools.elf.elffile import ELFFile
    from unicorn import (
        UC_ARCH_ARM,
        UC_HOOK_CODE,
        UC_MODE_MCLASS,
        UC_MODE_THUMB,
        UC_PROT_EXEC,
        UC_PROT_READ,
        UC_PROT_WRITE,
        Uc,
    )
    from unicorn.arm_const import (
        UC_ARM_REG_LR,
        UC_ARM_REG_PC,
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
        UC_ARM_REG_R3,
        UC_ARM_REG_R4,
        UC_ARM_REG_R5,
        UC_ARM_REG_R6,
        UC_ARM_REG_R7,
        UC_ARM_REG_R8,
        UC_ARM_REG_R9,
        UC_ARM_REG_R10,
        UC_ARM_REG_R11,
        UC_ARM_REG_SP,
    )
except ImportError as error:
    raise SystemExit("byte search instructions not run: install python3-unicorn and "
                     "python3-pyelftools for the selected PYTHON") from error


BASE = "cfe41205235bfc677e6fdf1a53a395aae2f6cb71"
SOURCES = {"memmem": "lib/libc/string/memmem.c", "memchr": "lib/libc/compat/memchr.c",
           "memcmp": "lib/libc/compat/memcmp.c"}
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
FLASH, RAM, STOP, STACK = 0x10020000, 0x20000000, 0x1002F000, 0x2002F000
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]
CASES = [("empty", b"", b"", "common"), ("oversized", b"a", b"aa", "common"),
         ("single-first", b":a:b:c", b":", "common"),
         ("single-middle", b"a:b:c:d", b":", "common"),
         ("single-absent", b"a"*4096, b":", "common"),
         ("single-last", b"a"*4095+b":", b":", "common"),
         ("pair-first", b"\xc2\xa7one\xc2\xa7two", b"\xc2\xa7", "common"),
         ("pair-absent", b"a"*4096, b"\xc2\xa7", "common"),
         ("pair-false-prefix", b"a"*4096, b"ab", "common"),
         ("binary-first", bytes(range(256))*4, bytes(range(16)), "common"),
         ("binary-last", b"x"*1024+bytes(range(16)), bytes(range(16)), "common"),
         ("periodic-absent", b"ab"*2048, b"ab"*31+b"ac", "adversarial"),
         ("repeat-prefix-absent", b"a"*2048, b"a"*127+b"b", "adversarial")]


class SearchFailure(RuntimeError):
    """A result, state, calibration or work bound fails."""


def require(condition, message):
    if not condition:
        raise SearchFailure(message)


def command(argv, cwd):
    result = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                            timeout=90, check=False)
    require(result.returncode == 0 and not result.stderr,
            "tool failure: " + result.stdout + result.stderr)
    return result.stdout


def build(root, compiler, work, label, sources):
    objects = []
    for name, text in sources.items():
        source = work / (label+"-"+name+".c")
        source.write_text(text)
        obj = source.with_suffix(".o")
        warnings = ["-Wstrict-prototypes", "-Wmissing-prototypes"]
        # C17 permits the pinned historical K&R definitions; modern production
        # memmem/memchr additionally reject them in the focused contract gate.
        command(compiler + FLAGS + warnings + ["-nostdinc", "-I"+str(root/"include"),
                "-c", str(source), "-o", str(obj)], root)
        objects.append(str(obj))
    image = work / (label+".elf")
    command(compiler + ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft",
            "-nostdlib", "-Wl,-Ttext=0x10020000", "-Wl,-e,memmem"] + objects +
            ["-o", str(image)], root)
    return image


def execute(image, hay, needle, count=True):
    elf = ELFFile(io.BytesIO(image.read_bytes()))
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.mem_map(FLASH, 65536, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC)
    for segment in elf.iter_segments():
        if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
            address = int(segment["p_vaddr"])
            require(FLASH <= address and address+segment["p_filesz"] <= FLASH+65536,
                    "image containment")
            uc.mem_write(address, segment.data())
    uc.mem_protect(FLASH, 65536, UC_PROT_READ | UC_PROT_EXEC)
    uc.mem_map(RAM, 196608, UC_PROT_READ | UC_PROT_WRITE)
    uc.mem_write(RAM, hay or b"\0")
    uc.mem_write(RAM+65536, needle or b"\0")
    uc.mem_protect(RAM, 131072, UC_PROT_READ)
    for register, value in [(UC_ARM_REG_R0, RAM), (UC_ARM_REG_R1, len(hay)),
                            (UC_ARM_REG_R2, RAM+65536), (UC_ARM_REG_R3, len(needle)),
                            (UC_ARM_REG_SP, STACK), (UC_ARM_REG_LR, STOP|1)]:
        uc.reg_write(register, value)
    for i, reg in enumerate(SAVED):
        uc.reg_write(reg, 0x12340000+i)
    counter = [0]

    def hook(_uc, _address, _size, _data):
        if count:
            counter[0] += 1

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(int(elf["e_entry"])|1, STOP, count=10000000)
    require(uc.reg_read(UC_ARM_REG_PC) == STOP, "execution instruction limit")
    offset = next((i for i in range(len(hay)-len(needle)+1)
                   if all(hay[i+j] == needle[j] for j in range(len(needle)))), -1)
    require(uc.reg_read(UC_ARM_REG_R0) == (RAM+offset if offset >= 0 else 0),
            "result oracle")
    require(uc.reg_read(UC_ARM_REG_SP) == STACK and
            all(uc.reg_read(reg) == 0x12340000+i for i, reg in enumerate(SAVED)),
            "callee state")
    require(counter[0] > 0, "instruction counter")
    return counter[0]


def check_budget(kind, observed, baseline):
    if kind == "common":
        require(observed <= baseline+96, "common instruction budget")
    else:
        require(observed <= baseline//4, "adversarial instruction budget")


def reject(action, expected, label):
    try:
        action()
    except SearchFailure as error:
        require(str(error) == expected, label+" rejects elsewhere: "+str(error))
    else:
        raise SearchFailure(label+" escapes rejection")
    print("byte search instruction control rejects: "+label, flush=True)


def qualify(root, compiler, work, provider=None):
    for name, version, floor in [("unicorn", unicorn.__version__, (2, 0)),
                                 ("pyelftools", elftools.__version__, (0, 29))]:
        parts = version.split(".")[:2]
        require(len(parts) == 2 and all(part.isdecimal() for part in parts) and
                tuple(int(part) for part in parts) >= floor,
                name+" version is outside the supported instruction-gate range")
    require(bool(compiler), "ARM compiler is empty")
    current = {name: (root/path).read_text() for name, path in SOURCES.items()}
    if provider:
        current["memmem"] = provider.read_text()
    baseline = {name: command(["git", "-c", "core.fsmonitor=false", "show", BASE+":"+path],
                              root) for name, path in SOURCES.items()}
    old = build(root, compiler, work, "baseline", baseline)
    good = build(root, compiler, work, "candidate", current)
    rows = []
    for name, hay, needle, kind in CASES:
        before, after = execute(old, hay, needle), execute(good, hay, needle)
        try:
            check_budget(kind, after, before)
        except SearchFailure as error:
            raise SearchFailure(f"{name}: {error}; observed={after}, baseline={before}") from error
        rows.append({"case": name, "class": kind, "haystack_bytes": len(hay),
                     "needle_bytes": len(needle), "baseline": before, "candidate": after})
    print("byte search instructions: 13 result/state/work cases pass", flush=True)
    anchor = "\tif (needle_length == 1)\n"
    require(current["memmem"].count(anchor) == 1, "fast-path anchor differs")
    replacement = ("\treturn search(haystack_pointer, haystack_length, "
                   "needle_pointer, needle_length);\n")
    changed = dict(current)
    changed["memmem"] = current["memmem"].replace(anchor, replacement+anchor)
    bypass = build(root, compiler, work, "bypass", changed)
    row = next(row for row in rows if row["case"] == "single-absent")
    reject(lambda: check_budget("common", execute(bypass, b"a"*4096, b":"),
                                row["baseline"]), "common instruction budget", "bypass fast paths")
    row = next(row for row in rows if row["case"] == "repeat-prefix-absent")
    reject(lambda: check_budget("adversarial", row["baseline"], row["baseline"]),
           "adversarial instruction budget", "quadratic scan")
    anchor = "return (void *)haystack_pointer;"
    require(current["memmem"].count(anchor) == 1, "empty-result anchor differs")
    changed["memmem"] = current["memmem"].replace(anchor, "return NULL;")
    wrong = build(root, compiler, work, "wrong", changed)
    reject(lambda: execute(wrong, b"", b""), "result oracle", "wrong result")
    reject(lambda: execute(good, b"ab", b"b", count=False),
           "instruction counter", "disabled counter")
    result = {"evidence_class": ("modeled ARM instruction execution; "
                                 "cycles and board timing unmeasured"),
              "baseline_commit": BASE, "unicorn_version": unicorn.__version__,
              "pyelftools_version": elftools.__version__,
              "compiler_version": command(compiler+["--version"], root).splitlines()[0],
              "flags": FLAGS, "source_sha256": {SOURCES[n]: hashlib.sha256(t.encode()).hexdigest()
                                                   for n, t in current.items()},
              "images": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (old, good)},
              "cases": rows, "controls": ["bypass fast paths", "quadratic scan", "wrong result",
                                           "disabled counter"]}
    (work/"receipt.json").write_text(json.dumps(result, indent=2)+"\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    args = parser.parse_args()
    root = args.root.resolve()
    evidence = root/".build-logs"
    require(not evidence.is_symlink(), "evidence root is a symlink")
    evidence.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="byte-search-instructions.", dir=evidence))
    try:
        qualify(root, shlex.split(args.compiler), work)
    except (SearchFailure, OSError, subprocess.SubprocessError, unicorn.UcError) as error:
        (work/"failure.json").write_text(json.dumps({"state": "failed", "error": str(error)},
                                                 indent=2)+"\n")
        raise SystemExit("FAIL byte search instructions: "+str(error)) from error


if __name__ == "__main__":
    main()
