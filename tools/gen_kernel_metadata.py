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


# A syscall trace name is a span of the name pool: the low
# NAME_OFFSET_BITS of its 16-bit table entry hold the offset and the rest
# hold the length.
NAME_OFFSET_BITS = 10
NAME_LENGTH_LIMIT = (1 << (16 - NAME_OFFSET_BITS)) - 1
NAME_POOL_LIMIT = 1 << NAME_OFFSET_BITS


def name_overlap(left: bytes, right: bytes) -> int:
    """Length of the longest proper suffix of left that begins right."""
    for length in range(min(len(left), len(right)) - 1, 0, -1):
        if left.endswith(right[:length]):
            return length
    return 0


def pack_names(names: list[bytes]) -> tuple[bytes, list[int]]:
    """Return a string that contains every name, and each name's offset.

    The names carry no terminator, so one name can lie inside another or
    across the seam of two more. Names inside other names are dropped, then
    the pair with the longest suffix-prefix overlap merges until no pair
    overlaps: the maximum-overlap greedy construction for the shortest
    common superstring. Ties resolve by byte order, so the pool depends on
    the set of names alone.
    """
    distinct = sorted(set(names))
    pieces = [
        name
        for name in distinct
        if not any(name != other and name in other for other in distinct)
    ]
    overlaps = {
        (left, right): name_overlap(left, right)
        for left in pieces
        for right in pieces
        if left != right
    }
    while overlaps:
        (left, right), length = min(
            overlaps.items(), key=lambda item: (-item[1], item[0])
        )
        if length == 0:
            break
        merged = left + right[length:]
        pieces = [piece for piece in pieces if piece not in (left, right)]
        overlaps = {
            pair: value
            for pair, value in overlaps.items()
            if left not in pair and right not in pair
        }
        for piece in pieces:
            overlaps[(merged, piece)] = name_overlap(merged, piece)
            overlaps[(piece, merged)] = name_overlap(piece, merged)
        pieces.append(merged)
    pool = b"".join(sorted(pieces))
    return pool, [pool.index(name) for name in names]


# An errno message is stored as symbols: a byte below RULE_BASE is a
# character, and a byte at or above it names a rule, a pair of symbols that
# each name a character or an earlier rule.
RULE_BASE = 128
RULE_LIMIT = 256 - RULE_BASE
RULE_DEPTH_LIMIT = 4


def pair_counts(sequences: list[list[int]]) -> dict[tuple[int, int], int]:
    """Count the non-overlapping occurrences of every adjacent pair."""
    counts: dict[tuple[int, int], int] = {}
    for sequence in sequences:
        skip = False
        for index in range(len(sequence) - 1):
            pair = (sequence[index], sequence[index + 1])
            # In a run of one symbol the pairs overlap; count every other one.
            if skip and pair[0] == pair[1] == sequence[index - 1]:
                skip = False
                continue
            counts[pair] = counts.get(pair, 0) + 1
            skip = pair[0] == pair[1]
    return counts


def replace_pair(sequence: list[int], pair: tuple[int, int], symbol: int) -> list[int]:
    result = []
    index = 0
    while index < len(sequence):
        if tuple(sequence[index : index + 2]) == pair:
            result.append(symbol)
            index += 2
        else:
            result.append(sequence[index])
            index += 1
    return result


def rule_depth(rules: list[tuple[int, int]], symbol: int) -> int:
    """Nested expansions of the first symbol, which the decoder recurses on."""
    depth = 0
    while symbol >= RULE_BASE:
        depth += 1
        symbol = rules[symbol - RULE_BASE][0]
    return depth


def compress_messages(
    messages: list[bytes],
) -> tuple[list[tuple[int, int]], list[bytes]]:
    """Return digram rules and the messages rewritten in them.

    Re-Pair: the most frequent adjacent pair of symbols becomes a rule and
    every occurrence is replaced by the rule's symbol, until RULE_LIMIT rules
    exist or no pair occurs three times. A rule costs its two bytes, so a
    pair that occurs twice saves nothing. A pair whose first symbol would
    nest beyond RULE_DEPTH_LIMIT is passed over, which bounds the decoder's
    recursion. Ties resolve by symbol value, so the result depends on the
    messages alone.
    """
    sequences = [list(message) for message in messages]
    rules: list[tuple[int, int]] = []
    while len(rules) < RULE_LIMIT:
        candidates = [
            (count, pair)
            for pair, count in pair_counts(sequences).items()
            if count >= 3 and rule_depth(rules, pair[0]) < RULE_DEPTH_LIMIT
        ]
        if not candidates:
            break
        pair = max(candidates)[1]
        symbol = RULE_BASE + len(rules)
        rules.append(pair)
        sequences = [replace_pair(sequence, pair, symbol) for sequence in sequences]
    return rules, [bytes(sequence) for sequence in sequences]


def expand_message(rules: list[tuple[int, int]], encoded: bytes) -> bytes:
    """The decoder of the generated kernel_errmsg(), for the round trip."""
    result = bytearray()

    def expand(symbol: int) -> None:
        while symbol >= RULE_BASE:
            first, symbol = rules[symbol - RULE_BASE]
            expand(first)
        result.append(symbol)

    for symbol in encoded:
        expand(symbol)
    return bytes(result)


def c_bytes(data: bytes) -> str:
    """A C string literal for data, with rule symbols as octal escapes.

    An escape is three digits, so a digit character after it stays a
    character.
    """
    parts = []
    for value in data:
        if not 0x20 <= value < 0x7F or value in (0x22, 0x5C, 0x3F):
            parts.append(f"\\{value:03o}")
        else:
            parts.append(chr(value))
    return '"' + "".join(parts) + '"'


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
        if (
            not trace_name
            or not trace_name.isascii()
            or not trace_name.isprintable()
            or any(character in trace_name for character in '"\\?')
            or "*/" in trace_name
        ):
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
    longest = max(len(call.trace_name) for call in syscalls)
    if longest > NAME_LENGTH_LIMIT:
        raise MetadataError(f"{path}: a syscall name is {longest} bytes")
    pool_size = len(pack_names([call.trace_name.encode() for call in syscalls])[0])
    if pool_size > NAME_POOL_LIMIT:
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
        if not message or not message.isascii() or "\0" in message:
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


def render_dispatch_header(syscalls: list[Syscall]) -> str:
    handlers = {call.handler for call in syscalls}
    if any(call.condition in {"IFNET", "GLOB"} for call in syscalls):
        handlers.add("nosys")
    if any(call.condition == "ERRNET" for call in syscalls):
        handlers.add("nonet")
    lines = [
        SYSCALL_TABLE_LICENSE,
        "\n",
        generated_notice("syscalls.master"),
        "#ifndef _SYS_SYSCALL_DISPATCH_H_\n#define _SYS_SYSCALL_DISPATCH_H_\n\n",
        "/* Kernel dispatch consumers use built-in C types.\n",
        " * Separate arrays keep pointers naturally aligned and counts byte-sized.\n",
        " */\n",
        "typedef void (*syscall_handler_t)(void);\n",
        "extern const unsigned char syscall_nargs[];\n",
        "extern const syscall_handler_t syscall_handlers[];\n",
        "extern const int nsysent;\n",
        "/* The name is *length bytes with no terminator. */\n",
        "const char *syscall_name(unsigned int code, unsigned int *length);\n\n",
        "/* Handlers read arguments and publish results through the kernel u-area. */\n",
    ]
    lines.extend(f"void {handler}(void);\n" for handler in sorted(handlers))
    lines.append("\n#endif /* _SYS_SYSCALL_DISPATCH_H_ */\n")
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
        "#include <sys/syscall_dispatch.h>\n#include <sys/stdint.h>\n\n",
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
    names = [call.trace_name.encode() for call in syscalls]
    pool, offsets = pack_names(names)
    spans = [
        offset | len(name) << NAME_OFFSET_BITS
        for name, offset in zip(names, offsets, strict=True)
    ]
    text = pool.decode()
    lines = [
        SYSCALL_TABLE_LICENSE,
        "\n",
        generated_notice("syscalls.master"),
        "#include <sys/syscall_dispatch.h>\n#include <sys/stdint.h>\n\n",
        "/*\n",
        " * Every trace name is a span of this string. The names carry no\n",
        " * terminator, so one name lies inside another or across the seam of\n",
        " * two more. A span holds the offset in its low "
        f"{NAME_OFFSET_BITS} bits and the\n",
        " * length above them.\n",
        " */\n",
        "static const char syscall_name_pool[] =\n",
    ]
    lines.extend(
        f'    "{text[index : index + 64]}"\n' for index in range(0, len(text), 64)
    )
    lines.append("    ;\n\nstatic const uint16_t syscall_name_spans[] = {\n")
    for index in range(0, len(spans), 10):
        values = ", ".join(str(value) for value in spans[index : index + 10])
        lines.append(f"    {values},\n")
    lines.extend(
        [
            "};\n\n",
            "_Static_assert(sizeof(syscall_name_spans) /\n",
            f"    sizeof(syscall_name_spans[0]) == {len(syscalls)},\n",
            '    "syscall name and ABI counts differ");\n\n',
            "const char *\n",
            "syscall_name(unsigned int code, unsigned int *length)\n{\n",
            "    unsigned int span;\n\n",
            "    if (code >= (unsigned int)nsysent) {\n",
            "        *length = 1;\n        return \"?\";\n    }\n",
            "    span = syscall_name_spans[code];\n",
            f"    *length = span >> {NAME_OFFSET_BITS};\n",
            "    return syscall_name_pool + "
            f"(span & {NAME_POOL_LIMIT - 1}U);\n}}\n",
        ]
    )
    return "".join(lines)


def render_errlist(messages: list[str]) -> str:
    plain = [message.encode() for message in messages]
    rules, encoded = compress_messages(plain)
    for message, symbols in zip(plain, encoded, strict=True):
        if expand_message(rules, symbols) != message:
            raise MetadataError(f"errno message {message!r} does not round-trip")
    offsets = []
    offset = 0
    for symbols in encoded:
        offsets.append(offset)
        offset += len(symbols) + 1
    longest = max(len(message) for message in plain) + 1
    lines = [
        SYSCALL_TABLE_LICENSE,
        "\n",
        generated_notice("errlist.master"),
        "#include <sys/param.h>\n#include <sys/errno.h>\n#include <sys/systm.h>\n\n",
        "/*\n",
        " * The messages are stored as a grammar. A byte below "
        f"{RULE_BASE} is a character;\n",
        f" * a byte from {RULE_BASE} up names a rule, two symbols that each "
        "name a character\n",
        " * or an earlier rule. A rule's first symbol nests at most "
        f"{RULE_DEPTH_LIMIT} rules deep.\n",
        " */\n",
        f"static const unsigned char errno_message_rules[{len(rules)}][2] = {{\n",
    ]
    for index in range(0, len(rules), 6):
        values = ", ".join(
            f"{{{first}, {second}}}" for first, second in rules[index : index + 6]
        )
        lines.append(f"    {values},\n")
    lines.append("};\n\nstatic const char errno_message_pool[] =\n")
    lines.extend(f"    {c_bytes(symbols + bytes(1))}\n" for symbols in encoded)
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
            '    "errno metadata must cover zero through ELAST");\n',
            f"_Static_assert(KERNEL_ERRMSG_MAX >= {longest},\n",
            '    "KERNEL_ERRMSG_MAX must hold the longest message");\n\n',
            "/* Append the expansion of one symbol and return the new end. */\n",
            "static char *\n",
            "errno_message_expand(char *end, unsigned int symbol)\n{\n",
            f"    while (symbol >= {RULE_BASE}) {{\n",
            "        const unsigned char *rule = "
            f"errno_message_rules[symbol - {RULE_BASE}];\n\n",
            "        end = errno_message_expand(end, rule[0]);\n",
            "        symbol = rule[1];\n    }\n",
            "    *end++ = (char)symbol;\n    return end;\n}\n\n",
            "/*\n",
            " * Write the message of error and its terminator into buffer, which\n",
            " * holds KERNEL_ERRMSG_MAX bytes, and return the count written; zero\n",
            " * when error has no message.\n",
            " */\n",
            "u_int\n",
            "kernel_errmsg(u_int error, char *buffer)\n{\n",
            "    const unsigned char *symbol;\n",
            "    char *end = buffer;\n\n",
            "    if (error >= ERRNO_MESSAGE_COUNT)\n        return 0;\n",
            "    symbol = (const unsigned char *)errno_message_pool +\n",
            "        errno_message_offsets[error];\n",
            "    while (*symbol != 0)\n",
            "        end = errno_message_expand(end, *symbol++);\n",
            "    *end++ = 0;\n",
            "    return (u_int)(end - buffer);\n}\n",
        ]
    )
    return "".join(lines)


def render_all(root: Path) -> dict[Path, str]:
    syscalls = parse_syscalls(root / "sys/kern/syscalls.master")
    messages = parse_errlist(root / "sys/kern/errlist.master")
    return {
        Path("include/syscall.h"): render_syscall_header(syscalls),
        Path("sys/sys/syscall_dispatch.h"): render_dispatch_header(syscalls),
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
        pool, offsets = pack_names([b"read", b"readv", b"pread", b"adv", b"vfork"])
        if pool != b"preadvfork" or offsets != [1, 1, 0, 3, 5]:
            raise MetadataError(f"name packing gave {pool!r} {offsets}")
        wide_pool = "".join(
            f"{number}|0|nosys|ALWAYS|{chr(65 + number) * 60}|-\n"
            for number in range(18)
        )
        expect_error("syscall name pool beyond its offset field",
                     lambda: syscall_case(wide_pool))
        expect_error("syscall name with a quote",
                     lambda: syscall_case(valid.replace("|exit|", '|ex"it|')))
        sample = [b"no such file", b"no such process", b"no such device"]
        rules, encoded = compress_messages(sample)
        if (
            not rules
            or [expand_message(rules, symbols) for symbols in encoded] != sample
            or sum(map(len, encoded)) + 2 * len(rules) >= sum(map(len, sample))
            or max(rule_depth(rules, first) for first, _ in rules) >= RULE_DEPTH_LIMIT
        ):
            raise MetadataError("errno message grammar self-test failed")
        if pair_counts([[7, 7, 7, 7, 7]]) != {(7, 7): 2}:
            raise MetadataError("overlapping pairs are counted once each")
        expect_error("errno message beyond ASCII",
                     lambda: errno_case("0|caf\u00e9\n"))
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
