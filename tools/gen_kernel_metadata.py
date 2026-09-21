"""Generate compact kernel syscall and errno metadata."""

from __future__ import annotations

import argparse
import difflib
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path

VALID_CONDITIONS = {"ALWAYS", "GLOB", "IFNET", "ERRNET"}
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

SYSCALL_HEADER_LICENSE = """/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
"""

SYSCALL_TABLE_LICENSE = """/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
"""


class MetadataError(ValueError):
    """Report an invalid declarative metadata record."""


@dataclass(frozen=True)
class PublicName:
    name: str
    number: int
    alias: bool


@dataclass(frozen=True)
class Syscall:
    number: int
    nargs: int
    handler: str
    condition: str
    trace_name: str
    public_names: tuple[PublicName, ...]


def c_string(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}\\0"'


def read_records(path: Path, fields: int) -> list[tuple[int, list[str]]]:
    records = []
    for line_number, raw_line in enumerate(path.read_text().splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("|", fields - 1)]
        if len(parts) != fields:
            raise MetadataError(
                f"{path}:{line_number}: expected {fields} fields, got {len(parts)}"
            )
        records.append((line_number, parts))
    return records


def parse_public(field: str, path: Path, line_number: int) -> tuple[PublicName, ...]:
    if field == "-":
        return ()
    result = []
    for item in field.split(","):
        try:
            name_field, number_field = item.split("=", 1)
            number = int(number_field, 10)
        except ValueError as error:
            raise MetadataError(
                f"{path}:{line_number}: invalid public definition {item!r}"
            ) from error
        alias = name_field.endswith("@alias")
        name = name_field.removesuffix("@alias")
        if not IDENTIFIER.fullmatch(name) or not 0 <= number <= 255:
            raise MetadataError(
                f"{path}:{line_number}: invalid public definition {item!r}"
            )
        result.append(PublicName(name, number, alias))
    return tuple(result)


def parse_syscalls(path: Path) -> list[Syscall]:
    syscalls = []
    public_names: set[str] = set()
    public_numbers: dict[int, str] = {}
    for line_number, parts in read_records(path, 6):
        number_field, nargs_field, handler, condition, trace_name, public = parts
        try:
            number = int(number_field, 10)
            nargs = int(nargs_field, 10)
        except ValueError as error:
            raise MetadataError(f"{path}:{line_number}: invalid integer") from error
        if number != len(syscalls):
            raise MetadataError(
                f"{path}:{line_number}: expected syscall {len(syscalls)}, got {number}"
            )
        if not 0 <= nargs <= 6:
            raise MetadataError(f"{path}:{line_number}: nargs {nargs} exceeds 0..6")
        if not IDENTIFIER.fullmatch(handler):
            raise MetadataError(f"{path}:{line_number}: invalid handler {handler!r}")
        if condition not in VALID_CONDITIONS:
            raise MetadataError(
                f"{path}:{line_number}: invalid condition {condition!r}"
            )
        if not trace_name or "\0" in trace_name or "*/" in trace_name:
            raise MetadataError(f"{path}:{line_number}: invalid trace name")
        definitions = parse_public(public, path, line_number)
        for definition in definitions:
            if definition.name in public_names:
                raise MetadataError(
                    f"{path}:{line_number}: duplicate public name {definition.name}"
                )
            public_names.add(definition.name)
            owner = public_numbers.get(definition.number)
            if definition.alias and owner is None:
                raise MetadataError(
                    f"{path}:{line_number}: public alias {definition.name} "
                    f"has no canonical owner for number {definition.number}"
                )
            if owner is not None and not definition.alias:
                raise MetadataError(
                    f"{path}:{line_number}: public number {definition.number} "
                    f"already belongs to {owner}; mark an intentional alias"
                )
            if owner is None:
                public_numbers[definition.number] = definition.name
            if not definition.alias and definition.number != number:
                raise MetadataError(
                    f"{path}:{line_number}: public {definition.name} must use slot {number}"
                )
        if number > 255:
            raise MetadataError(
                f"{path}:{line_number}: syscall {number} exceeds the 8-bit ABI"
            )
        syscalls.append(
            Syscall(number, nargs, handler, condition, trace_name, definitions)
        )
    if not syscalls:
        raise MetadataError(f"{path}: empty syscall master")
    pool_size = sum(len(call.trace_name.encode("utf-8")) + 1 for call in syscalls)
    if pool_size > 0xFFFF:
        raise MetadataError(f"{path}: syscall name pool is {pool_size} bytes")
    return syscalls


def parse_errlist(path: Path) -> list[str]:
    messages = []
    for line_number, parts in read_records(path, 2):
        number_field, message = parts
        try:
            number = int(number_field, 10)
        except ValueError as error:
            raise MetadataError(f"{path}:{line_number}: invalid errno") from error
        if number != len(messages):
            raise MetadataError(
                f"{path}:{line_number}: expected errno {len(messages)}, got {number}"
            )
        if not message or "\0" in message:
            raise MetadataError(f"{path}:{line_number}: invalid error message")
        messages.append(message)
    if not messages:
        raise MetadataError(f"{path}: empty errno master")
    pool_size = sum(len(message.encode("utf-8")) + 1 for message in messages)
    if pool_size > 0xFFFF:
        raise MetadataError(f"{path}: errno message pool is {pool_size} bytes")
    return messages


def generated_notice(master: str) -> str:
    return (
        "/*\n"
        " * Generated by tools/gen_kernel_metadata.py from "
        f"sys/kern/{master}.\n"
        " * Edit the master and run `bmake regen-kernel-metadata`.\n"
        " */\n"
    )


def render_syscall_header(syscalls: list[Syscall]) -> str:
    lines = [
        SYSCALL_HEADER_LICENSE,
        "\n",
        generated_notice("syscalls.master"),
        "#ifndef _SYSCALL_H_\n#define _SYSCALL_H_\n\n",
    ]
    for call in syscalls:
        for definition in call.public_names:
            lines.append(f"#define SYS_{definition.name:<11} {definition.number}\n")
    lines.append(f"\n#define SYS_MAXSYSCALL {len(syscalls)}\n\n#endif\n")
    return "".join(lines)


def conditional_nargs(call: Syscall) -> list[str]:
    if call.condition in {"IFNET", "ERRNET"}:
        return [
            "#ifdef INET\n",
            f"    {call.nargs},\n",
            "#else\n",
            "    0,\n",
            "#endif\n",
        ]
    return [f"    {call.nargs},\n"]


def conditional_handler(call: Syscall) -> list[str]:
    if call.condition in {"IFNET", "ERRNET"}:
        fallback = "nonet" if call.condition == "ERRNET" else "nosys"
        return [
            "#ifdef INET\n",
            f"    {call.handler},\n",
            "#else\n",
            f"    {fallback},\n",
            "#endif\n",
        ]
    if call.condition == "GLOB":
        return [
            "#ifdef GLOB_ENABLED\n",
            f"    {call.handler},\n",
            "#else\n",
            "    nosys,\n",
            "#endif\n",
        ]
    return [f"    {call.handler},\n"]


def render_init_sysent(syscalls: list[Syscall]) -> str:
    lines = [
        SYSCALL_TABLE_LICENSE,
        "\n",
        generated_notice("syscalls.master"),
        "#include <sys/param.h>\n#include <sys/systm.h>\n\n",
        "extern void sc_msec(void);\n\n",
        "const uint8_t syscall_nargs[] = {\n",
    ]
    for call in syscalls:
        lines.extend(conditional_nargs(call))
    lines.append("};\n\nconst syscall_handler_t syscall_handlers[] = {\n")
    for call in syscalls:
        lines.extend(conditional_handler(call))
    lines.extend(
        [
            "};\n\n",
            "_Static_assert(sizeof(syscall_nargs) / sizeof(syscall_nargs[0]) ==\n",
            "    sizeof(syscall_handlers) / sizeof(syscall_handlers[0]),\n",
            '    "syscall metadata arrays differ in length");\n\n',
            "_Static_assert(sizeof(syscall_nargs) / sizeof(syscall_nargs[0]) ==\n",
            f"    {len(syscalls)}, \"syscall master count differs from ABI\");\n\n",
            "const int nsysent =\n",
            "    sizeof(syscall_handlers) / sizeof(syscall_handlers[0]);\n",
        ]
    )
    return "".join(lines)


def render_syscall_names(syscalls: list[Syscall]) -> str:
    offsets = []
    offset = 0
    for call in syscalls:
        offsets.append(offset)
        offset += len(call.trace_name.encode("utf-8")) + 1
    lines = [
        SYSCALL_TABLE_LICENSE,
        "\n",
        generated_notice("syscalls.master"),
        "#include <sys/param.h>\n#include <sys/systm.h>\n\n",
        "static const char syscall_name_pool[] =\n",
    ]
    lines.extend(f"    {c_string(call.trace_name)}\n" for call in syscalls)
    lines.append("    ;\n\nstatic const uint16_t syscall_name_offsets[] = {\n")
    for index in range(0, len(offsets), 12):
        values = ", ".join(str(value) for value in offsets[index : index + 12])
        lines.append(f"    {values},\n")
    lines.extend(
        [
            "};\n\n",
            "_Static_assert(sizeof(syscall_name_offsets) /\n",
            f"    sizeof(syscall_name_offsets[0]) == {len(syscalls)},\n",
            '    "syscall name and ABI counts differ");\n\n',
            "const char *\n",
            "syscall_name(u_int code)\n{\n",
            "    if (code >= (u_int)nsysent)\n        return \"?\";\n",
            "    return syscall_name_pool + syscall_name_offsets[code];\n}\n",
        ]
    )
    return "".join(lines)


def render_errlist(messages: list[str]) -> str:
    offsets = []
    offset = 0
    for message in messages:
        offsets.append(offset)
        offset += len(message.encode("utf-8")) + 1
    lines = [
        SYSCALL_TABLE_LICENSE,
        "\n",
        generated_notice("errlist.master"),
        "#include <sys/param.h>\n#include <sys/errno.h>\n#include <sys/systm.h>\n\n",
        "static const char errno_message_pool[] =\n",
    ]
    lines.extend(f"    {c_string(message)}\n" for message in messages)
    lines.append("    ;\n\nstatic const uint16_t errno_message_offsets[] = {\n")
    for index in range(0, len(offsets), 12):
        values = ", ".join(str(value) for value in offsets[index : index + 12])
        lines.append(f"    {values},\n")
    lines.extend(
        [
            "};\n\n",
            "#define ERRNO_MESSAGE_COUNT \\\n",
            "    (sizeof(errno_message_offsets) / sizeof(errno_message_offsets[0]))\n",
            "_Static_assert(ERRNO_MESSAGE_COUNT == ELAST + 1,\n",
            '    "errno metadata must cover zero through ELAST");\n\n',
            "const char *\n",
            "kernel_errmsg(u_int error)\n{\n",
            "    if (error >= ERRNO_MESSAGE_COUNT)\n        return NULL;\n",
            "    return errno_message_pool + errno_message_offsets[error];\n}\n",
        ]
    )
    return "".join(lines)


def render_all(root: Path) -> dict[Path, str]:
    syscalls = parse_syscalls(root / "sys/kern/syscalls.master")
    messages = parse_errlist(root / "sys/kern/errlist.master")
    return {
        Path("include/syscall.h"): render_syscall_header(syscalls),
        Path("sys/kern/init_sysent.c"): render_init_sysent(syscalls),
        Path("sys/kern/syscalls.c"): render_syscall_names(syscalls),
        Path("sys/kern/errlist.c"): render_errlist(messages),
    }


def write_outputs(output_root: Path, outputs: dict[Path, str]) -> None:
    for relative_path, content in outputs.items():
        output_path = output_root / relative_path
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(content)


def check_outputs(root: Path, outputs: dict[Path, str]) -> bool:
    matched = True
    with tempfile.TemporaryDirectory(prefix="kernel-metadata-") as directory:
        generated_root = Path(directory)
        write_outputs(generated_root, outputs)
        for relative_path in outputs:
            expected = (root / relative_path).read_text().splitlines(keepends=True)
            actual = (generated_root / relative_path).read_text().splitlines(
                keepends=True
            )
            if expected == actual:
                continue
            matched = False
            print(
                "".join(
                    difflib.unified_diff(
                        expected,
                        actual,
                        fromfile=str(root / relative_path),
                        tofile=str(generated_root / relative_path),
                    )
                ),
                end="",
            )
    return matched


def expect_error(label: str, action: object) -> None:
    try:
        action()  # type: ignore[operator]
    except MetadataError:
        return
    raise AssertionError(f"self-test accepted {label}")


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="kernel-metadata-selftest-") as directory:
        root = Path(directory)
        valid = "0|0|nosys|ALWAYS|indir|-\n1|1|rexit|ALWAYS|exit|exit=1\n"
        errno_valid = "0|Undefined error: 0\n1|Operation not permitted\n"

        def syscall_case(content: str) -> object:
            path = root / "syscalls.master"
            path.write_text(content)
            return parse_syscalls(path)

        def errno_case(content: str) -> object:
            path = root / "errlist.master"
            path.write_text(content)
            return parse_errlist(path)

        syscall_case(valid)
        errno_case(errno_valid)
        expect_error(
            "duplicate ordinal",
            lambda: syscall_case(valid + valid.splitlines()[1] + "\n"),
        )
        expect_error("missing ordinal", lambda: syscall_case(valid.replace("1|1|", "2|1|")))
        expect_error("nargs 7", lambda: syscall_case(valid.replace("1|1|", "1|7|")))
        expect_error(
            "invalid condition",
            lambda: syscall_case(valid.replace("ALWAYS|exit", "MAYBE|exit")),
        )
        expect_error(
            "public ABI alias collision",
            lambda: syscall_case(valid.replace("exit=1", "exit=1,other=1")),
        )
        expect_error(
            "alias without canonical owner",
            lambda: syscall_case(valid.replace("exit=1", "other@alias=2")),
        )
        expect_error(
            "alias before canonical owner",
            lambda: syscall_case(valid.replace("exit=1", "other@alias=1,exit=1")),
        )
        oversized_abi = "".join(
            f"{number}|0|nosys|ALWAYS|#{number}|-\n" for number in range(257)
        )
        expect_error("syscall 256", lambda: syscall_case(oversized_abi))
        huge_name = "x" * 65536
        expect_error(
            "oversized syscall pool",
            lambda: syscall_case(f"0|0|nosys|ALWAYS|{huge_name}|-\n"),
        )
        huge_message = "x" * 65536
        expect_error(
            "oversized errno pool", lambda: errno_case(f"0|{huge_message}\n")
        )
        expect_error("missing errno", lambda: errno_case("1|wrong\n"))
    print("kernel metadata generator self-test: ok")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    outputs = render_all(arguments.root)
    if arguments.check:
        return 0 if check_outputs(arguments.root, outputs) else 1
    write_outputs(arguments.output_root or arguments.root, outputs)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
