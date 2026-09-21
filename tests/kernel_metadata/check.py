"""Check compact kernel metadata and capacity instrumentation contracts."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


class CheckError(RuntimeError):
    """Report a contract mismatch."""


def records(path: Path, fields: int) -> list[list[str]]:
    result = []
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if line and not line.startswith("#"):
            parts = line.split("|", fields - 1)
            if len(parts) != fields:
                raise CheckError(f"{path}: malformed record")
            result.append(parts)
    return result


def count_initializer_rows(source: str, declaration: str) -> int:
    try:
        body = source.split(declaration, 1)[1].split("\n};", 1)[0]
    except IndexError as error:
        raise CheckError(f"missing initializer {declaration}") from error
    return len(re.findall(r"^\s*\{", body, re.MULTILINE))


def array_values(source: str, element_type: str, name: str) -> list[int]:
    pattern = rf"const {element_type} {name}\[NSPEEDS\] = \{{(.*?)\n\}};"
    match = re.search(pattern, source, re.DOTALL)
    if match is None:
        raise CheckError(f"{name} does not use {element_type}")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    return [int(value) for value in re.findall(r"\b\d+\b", body)]


def stack_usage(words: list[int], sentinel: int) -> tuple[int, int]:
    untouched = 0
    for word in words:
        if word != sentinel:
            break
        untouched += 4
    return len(words) * 4 - untouched, untouched


def check_tree(root: Path, overrides: dict[str, str] | None = None) -> None:
    overrides = overrides or {}

    def source(relative_path: str) -> str:
        return overrides.get(relative_path, (root / relative_path).read_text())

    syscall_records = records(root / "sys/kern/syscalls.master", 6)
    if len(syscall_records) != 156:
        raise CheckError("syscall master must contain slots 0 through 155")
    if syscall_records[23][2:5] != ["__sysctl", "ALWAYS", "__sysctl"]:
        raise CheckError("syscall 23 must remain __sysctl")
    if syscall_records[66][2] != "vfork":
        raise CheckError("dispatch slot 66 must remain vfork")
    public = source("include/syscall.h")
    generated_files = {
        "include/syscall.h": "Copyright (c) 1980 Regents",
        "sys/kern/init_sysent.c": "Copyright (c) 1986 Regents",
        "sys/kern/syscalls.c": "Copyright (c) 1986 Regents",
        "sys/kern/errlist.c": "Copyright (c) 1986 Regents",
    }
    for relative_path, notice in generated_files.items():
        if notice not in source(relative_path):
            raise CheckError(f"{relative_path} lost its historical license notice")
    if not re.search(r"^#define SYS_vfork\s+2$", public, re.MULTILINE):
        raise CheckError("SYS_vfork must remain the compatibility alias for fork")
    if not re.search(r"^#define SYS_sbrk\s+69$", public, re.MULTILINE):
        raise CheckError("SYS_sbrk must retain its public spelling")

    errno_records = records(root / "sys/kern/errlist.master", 2)
    if len(errno_records) != 82 or errno_records[-1][0] != "81":
        raise CheckError("errno master must cover zero through ELAST")

    conf = source("sys/arch/rp2040/rp2040/conf.c")
    if count_initializer_rows(conf, "const struct bdevsw bdevsw[] = {") != 6:
        raise CheckError("RP2040 bdevsw must contain majors 0..4 and its terminator")
    # PTY_ENABLED contributes one alternate source row, so the source has 12
    # initializers while either preprocessed table has majors 0..9 plus the
    # terminator.
    if count_initializer_rows(conf, "const struct cdevsw cdevsw[] = {") != 12:
        raise CheckError("RP2040 cdevsw must retain only majors 0..9")

    tty = source("sys/kern/tty.c")
    high = array_values(tty, "uint16_t", "tthiwat")
    low = array_values(tty, "uint8_t", "ttlowat")
    if len(high) != 29 or max(high) != 2000:
        raise CheckError("tty high-water values drifted")
    if len(low) != 29 or max(low) != 125:
        raise CheckError("tty low-water values drifted")

    aligned_cfree = "_Alignas(sizeof(struct cblock)) struct cblock cfree[NCLIST];"
    for architecture in ("rp2040", "stm32"):
        relative_path = f"sys/arch/{architecture}/{architecture}/machdep.c"
        if aligned_cfree not in source(relative_path):
            raise CheckError(f"{relative_path} can discard the first clist block")

    capacity_seams = {
        "sys/kern/kern_fork.c": ("CAPACITY_PROC", True, True),
        "sys/kern/kern_descrip.c": ("CAPACITY_FILE", True, True),
        "sys/kern/ufs_inode.c": ("CAPACITY_INODE", True, True),
        "sys/kern/tty_subr.c": ("CAPACITY_CLIST", True, True),
        "sys/kern/ufs_bio.c": ("CAPACITY_BUFFER", False, True),
        "sys/sys/buf.h": ("CAPACITY_BUFFER", True, False),
    }
    for relative_path, (kind, success, failure) in capacity_seams.items():
        if success and f"capacity_note({kind}, 0)" not in source(relative_path):
            raise CheckError(f"{relative_path} lacks its successful allocation event")
        if failure and f"capacity_note({kind}, 1)" not in source(relative_path):
            raise CheckError(f"{relative_path} lacks its exhaustion event")

    capacity = source("sys/kern/subr_capacity.c")
    if "live += proc[index].p_stat != 0;" not in capacity:
        raise CheckError("process occupancy must include live and zombie slots")
    if "live = NCLIST - cfreecount / CBSIZE;" not in capacity:
        raise CheckError("clist occupancy must convert free payload bytes to blocks")

    locore = source("sys/arch/rp2040/rp2040/locore0.S")
    reset_order = (
        "ldr\tr2, =(u0 + UAREA_STACK_OFFSET)",
        "ldr\tr2, =(u + UAREA_STACK_OFFSET)",
        "bl\tSystemInit",
    )
    positions = [locore.find(fragment) for fragment in reset_order]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise CheckError("both u-area stack paints must precede SystemInit")

    for architecture in ("rp2040", "stm32", "pic32"):
        compile_root = root / "sys/arch" / architecture / "compile"
        for makefile in compile_root.glob("*/Makefile"):
            makefile_source = makefile.read_text()
            if "errlist.o" not in makefile_source or "$S/kern/errlist.c" not in makefile_source:
                raise CheckError(f"{makefile}: generated kernel metadata source is absent")


def expect_bad(label: str, action: object) -> None:
    try:
        action()  # type: ignore[operator]
    except CheckError:
        return
    raise AssertionError(f"known-bad fixture passed: {label}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    root = arguments.root
    check_tree(root)

    conf_path = "sys/arch/rp2040/rp2040/conf.c"
    conf = (root / conf_path).read_text()
    extra_device = conf.replace(
        "\n\t/*\n\t * End the list with a blank entry.\n\t */\n\t{ 0 },",
        "\n\t{ NOCDEV }\n\t/*\n\t * End the list with a blank entry.\n\t */\n\t{ 0 },",
        1,
    )
    expect_bad(
        "trailing device row",
        lambda: check_tree(root, {conf_path: extra_device}),
    )

    tty_path = "sys/kern/tty.c"
    tty = (root / tty_path).read_text()
    expect_bad(
        "wide tty high-water table",
        lambda: check_tree(
            root, {tty_path: tty.replace("const uint16_t tthiwat", "const int tthiwat")}
        ),
    )

    machdep_path = "sys/arch/rp2040/rp2040/machdep.c"
    machdep = (root / machdep_path).read_text()
    expect_bad(
        "unaligned clist pool",
        lambda: check_tree(
            root,
            {
                machdep_path: machdep.replace(
                    "_Alignas(sizeof(struct cblock)) struct cblock cfree[NCLIST];",
                    "struct cblock cfree[NCLIST];",
                    1,
                )
            },
        ),
    )

    sentinel = 0xA5C35A3C
    words = [sentinel] * 512
    if stack_usage(words, sentinel) != (0, 2048):
        raise CheckError("untouched stack fixture failed")
    words[-32:] = [0] * 32
    if stack_usage(words, sentinel) != (128, 1920):
        raise CheckError("used stack fixture failed")
    words[0] = 0
    if stack_usage(words, sentinel)[1] != 0:
        raise CheckError("saturated stack fixture failed")

    locore_path = "sys/arch/rp2040/rp2040/locore0.S"
    locore = (root / locore_path).read_text()
    expect_bad(
        "missing u0 stack paint",
        lambda: check_tree(
            root,
            {
                locore_path: locore.replace(
                    "ldr\tr2, =(u0 + UAREA_STACK_OFFSET)",
                    "ldr\tr2, =(u + UAREA_STACK_OFFSET)",
                    1,
                )
            },
        ),
    )

    print("kernel metadata and capacity contracts: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
