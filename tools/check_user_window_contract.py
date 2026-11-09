"""Bind every statement of the RP2040 user-window bound to machparam.h.

USER_DATA_SIZE in sys/arch/rp2040/include/machparam.h is the one source of
the window size. The production consumers (the USER_TOP and
USER_WINDOW_MAX macros in sys/sys/systm.h, the USERRAM and RAM regions of
conf/RP2040.ld and the two user regions of rp2040/mpu.c) and the host
fixtures that model the window instead of including those headers must all
state the same bound. A fixture that keeps an old bound, or a production
macro that grants bytes past the window, passes its own tests while user
copy or exec admits kernel RAM; this checker rejects both.
"""

import argparse
import re
import sys
from pathlib import Path

USER_BASE = 0x20000000

MACHPARAM = "sys/arch/rp2040/include/machparam.h"
SYSTM = "sys/sys/systm.h"
LINKER = "sys/arch/rp2040/conf/RP2040.ld"
MPU = "sys/arch/rp2040/rp2040/mpu.c"
EXEC_ADMISSION = "tests/kernel/exec_elf_admission_test.c"
EXEC_SURROUNDING = "tests/kernel/exec_elf_surrounding_contract_test.c"
USERCOPY = "tests/kernel/usercopy_span_test.c"
RWUIO = "tests/kernel/rwuio_test.c"
AOUTRUN = "tests/qemu_aout/aoutrun.c"
USERCOPY_MODEL = "tests/rp2040/usercopy_span/check.py"

FILES = (
    MACHPARAM,
    SYSTM,
    LINKER,
    MPU,
    EXEC_ADMISSION,
    EXEC_SURROUNDING,
    USERCOPY,
    RWUIO,
    AOUTRUN,
    USERCOPY_MODEL,
)

SYSTM_USER_TOP = "#define USER_TOP(process) ((size_t)__user_data_end)"
SYSTM_WINDOW_MAX = "#define USER_WINDOW_MAX MAXMEM"


def single(pattern: str, text: str, path: str, errors: list[str]) -> str | None:
    """Return the one capture of pattern in text, or record why not."""
    found = re.findall(pattern, text, re.MULTILINE)
    if len(found) != 1:
        errors.append(f"{path}: expected one match of {pattern!r}, found {len(found)}")
        return None
    return found[0]


def check(texts: dict[str, str]) -> list[str]:
    """Return every disagreement with the machparam.h window."""
    errors: list[str] = []
    kib = single(
        r"^#define\s+USER_DATA_SIZE\s+\((\d+)\s*\*\s*1024\)",
        texts[MACHPARAM],
        MACHPARAM,
        errors,
    )
    if kib is None:
        return errors
    size = int(kib) * 1024
    top = USER_BASE + size
    if single(
        r"^#define\s+USER_DATA_START\s+\((0x[0-9a-fA-F]+)\)", texts[MACHPARAM], MACHPARAM, errors
    ) not in (
        None,
        hex(USER_BASE),
    ):
        errors.append(f"{MACHPARAM}: USER_DATA_START is not {USER_BASE:#x}")
    if single(r"^#define\s+MAXMEM\s+(\S+)\s*$", texts[MACHPARAM], MACHPARAM, errors) not in (
        None,
        "USER_DATA_SIZE",
    ):
        errors.append(f"{MACHPARAM}: MAXMEM is not USER_DATA_SIZE")

    systm_lines = {line.strip() for line in texts[SYSTM].splitlines()}
    for line in (SYSTM_USER_TOP, SYSTM_WINDOW_MAX):
        if line not in systm_lines:
            errors.append(f"{SYSTM}: missing {line!r}")

    userram = single(
        r"^\s*USERRAM\s*\(rwx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+),\s*LENGTH\s*=\s*(\d+)K",
        texts[LINKER],
        LINKER,
        errors,
    )
    if userram is not None:
        if int(userram[0], 16) != USER_BASE or int(userram[1]) * 1024 != size:
            errors.append(
                f"{LINKER}: USERRAM is {userram[0]}+{userram[1]}K, expected {USER_BASE:#x}+{kib}K"
            )
    ram = single(
        r"^\s*RAM\s*\(rw!x\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)", texts[LINKER], LINKER, errors
    )
    if ram is not None and int(ram, 16) != top:
        errors.append(f"{LINKER}: RAM starts at {ram}, expected {top:#x}")

    low = single(r"^#define\s+MPU_USER_LOW_LOG2\s+(\d+)", texts[MPU], MPU, errors)
    high = single(r"^#define\s+MPU_USER_HIGH_LOG2\s+(\d+)", texts[MPU], MPU, errors)
    if low is not None and high is not None and (1 << int(low)) + (1 << int(high)) != size:
        errors.append(f"{MPU}: user regions 2^{low} + 2^{high} do not tile {size} bytes")

    for path in (EXEC_ADMISSION, EXEC_SURROUNDING):
        window = single(
            r"^#define\s+USER_WINDOW\s+\((\d+)U\s*\*\s*1024U\)", texts[path], path, errors
        )
        if window is not None and int(window) != int(kib):
            errors.append(f"{path}: USER_WINDOW is {window} KiB, expected {kib}")
        if single(r"^#define\s+USER_WINDOW_MAX\s+(\S+)\s*$", texts[path], path, errors) not in (
            None,
            "USER_WINDOW",
        ):
            errors.append(f"{path}: USER_WINDOW_MAX is not USER_WINDOW")

    fixture_top = single(
        r"^#define\s+USER_TOP\(process\)\s+(0x[0-9a-fA-F]+)U", texts[USERCOPY], USERCOPY, errors
    )
    if fixture_top is not None and int(fixture_top, 16) != top:
        errors.append(f"{USERCOPY}: USER_TOP is {fixture_top}, expected {top:#x}")
    upper = single(r"user_span_upper\s*=\s*(0x[0-9a-fA-F]+)UL;", texts[RWUIO], RWUIO, errors)
    if upper is not None and int(upper, 16) != top:
        errors.append(f"{RWUIO}: user_span_upper is {upper}, expected {top:#x}")
    loader = single(
        r"^#define\s+USER_DATA_SIZE\s+\((\d+)u\s*\*\s*1024u\)", texts[AOUTRUN], AOUTRUN, errors
    )
    if loader is not None and int(loader) != int(kib):
        errors.append(f"{AOUTRUN}: USER_DATA_SIZE is {loader} KiB, expected {kib}")

    model_tops = re.findall(
        r"rp2040_user_span_contains\(0x20000000U,\s*(0x[0-9a-fA-F]+)U", texts[USERCOPY_MODEL]
    )
    if not model_tops:
        errors.append(f"{USERCOPY_MODEL}: no rp2040_user_span_contains window cases")
    for value in model_tops:
        if int(value, 16) != top:
            errors.append(f"{USERCOPY_MODEL}: window case ends at {value}, expected {top:#x}")
    return errors


def mutate(texts: dict[str, str], path: str, old: str, new: str) -> dict[str, str]:
    """Return a copy of texts with one exact replacement in path."""
    if texts[path].count(old) < 1:
        raise SystemExit(f"self-test: {path} lacks {old!r}")
    changed = dict(texts)
    changed[path] = texts[path].replace(old, new, 1)
    return changed


def self_test(texts: dict[str, str]) -> None:
    """Require the tree to pass and each named defect to be rejected."""
    if check(texts):
        raise SystemExit("self-test: the unmodified tree is rejected")
    kib = re.search(r"USER_DATA_SIZE\s+\((\d+)\s*\*\s*1024\)", texts[MACHPARAM]).group(1)
    top = f"{USER_BASE + int(kib) * 1024:#x}"
    cases = {
        "production USER_TOP grants a bonus": (
            SYSTM,
            SYSTM_USER_TOP,
            "#define USER_TOP(process) ((size_t)__user_data_end + SWAPRAM_BONUS)",
        ),
        "production USER_WINDOW_MAX grants a bonus": (
            SYSTM,
            SYSTM_WINDOW_MAX,
            "#define USER_WINDOW_MAX (MAXMEM + SWAPRAM_BONUS)",
        ),
        "MAXMEM detached from USER_DATA_SIZE": (
            MACHPARAM,
            "MAXMEM                  USER_DATA_SIZE",
            "MAXMEM (147456)",
        ),
        "linker window shorter than machparam": (LINKER, f"LENGTH = {kib}K", "LENGTH = 144K"),
        "kernel RAM origin off the window end": (LINKER, f"ORIGIN = {top}", "ORIGIN = 0x20024000"),
        "MPU regions short of the window": (
            MPU,
            "MPU_USER_HIGH_LOG2\t16",
            "MPU_USER_HIGH_LOG2\t14",
        ),
        "exec fixture keeps the old window": (
            EXEC_ADMISSION,
            f"({kib}U * 1024U)",
            "(144U * 1024U)",
        ),
        "exec surrounding fixture keeps the old window": (
            EXEC_SURROUNDING,
            f"({kib}U * 1024U)",
            "(144U * 1024U)",
        ),
        "user-copy fixture keeps the old top": (USERCOPY, f"{top}U", "0x20024000U"),
        "rwuio fixture keeps the old top": (
            RWUIO,
            f"user_span_upper = {top}UL",
            "user_span_upper = 0x20024000UL",
        ),
        "qemu loader keeps the old window": (AOUTRUN, f"({kib}u * 1024u)", "(144u * 1024u)"),
        "user-copy model keeps the old top": (
            USERCOPY_MODEL,
            f"rp2040_user_span_contains(0x20000000U, {top}U",
            "rp2040_user_span_contains(0x20000000U, 0x20024000U",
        ),
    }
    for name, (path, old, new) in cases.items():
        if not check(mutate(texts, path, old, new)):
            raise SystemExit(f"self-test: {name} was accepted")
        print(f"PASS user-window mutation rejected: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    texts = {path: (arguments.root / path).read_text() for path in FILES}
    if arguments.self_test:
        self_test(texts)
    errors = check(texts)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"user-window contract: {len(FILES)} files agree with machparam.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
