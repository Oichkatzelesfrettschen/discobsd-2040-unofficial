#!/usr/bin/env python3
"""Inventory bounded API evidence in DiscoBSD and a 4.4BSD reference tree."""

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools" / "c17"))

from kr_scan import (  # noqa: E402
    ATTRIBUTE_WORDS,
    IDENT,
    NOT_NAMES,
    TYPE_WORDS,
    apply_conditionals,
    closing,
    conditional_events,
    continues_declaration,
    declaration_prefix,
    definition_follows,
    following_attributes,
    join_splices,
    statement_start,
    tokenize,
)

DECLARATION_SUBTREES = ("include", "sys/sys")
SYMBOL_SUBTREES = ("lib/libc",)
REQUIRED_SUBTREES = DECLARATION_SUBTREES + SYMBOL_SUBTREES


class InventoryError(Exception):
    """A requested inventory input cannot support the declared scan."""


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--discobsd-root", required=True, type=Path)
    parser.add_argument("--bsd44-root", required=True, type=Path)
    return parser, parser.parse_args(argv)


def validate_root(label, path):
    try:
        root = path.expanduser().resolve(strict=True)
    except OSError as error:
        raise InventoryError(f"{label} root is not a directory: {path}") from error
    if not root.is_dir():
        raise InventoryError(f"{label} root is not a directory: {path}")
    for subtree in REQUIRED_SUBTREES:
        if not (root / subtree).is_dir():
            raise InventoryError(
                f"{label} root lacks required subtree {subtree}: {path}"
            )
    return root


def source_files(root, subtrees, suffix):
    paths = []
    for subtree in subtrees:
        paths.extend(
            path
            for path in (root / subtree).rglob(f"*{suffix}")
            if path.is_file()
        )
    return sorted(set(paths))


def continuation_declaration_prefix(tokens, index):
    """Return whether index starts another function declarator after a comma."""
    start = statement_start(tokens, index)
    depth = 0
    separator = -1
    for position in range(start, index):
        token = tokens[position][0]
        if token in "([":
            depth += 1
        elif token in ")]":
            depth -= 1
        elif token == "," and depth == 0:
            separator = position
    if separator < 0 or not continues_declaration(tokens, index):
        return False

    position = separator + 1
    while position < index:
        token = tokens[position][0]
        if (
            token in ATTRIBUTE_WORDS
            and position + 1 < index
            and tokens[position + 1][0] == "("
        ):
            position = closing(tokens, position + 1) + 1
            if position <= 0 or position > index:
                return False
            continue
        if token != "*" and not (IDENT.fullmatch(token) and token not in NOT_NAMES):
            return False
        position += 1
    return True


def function_candidates(text):
    """Return lexical file-scope declaration and definition candidates."""
    conditionals = []
    tokens = tokenize(join_splices(text), conditionals)
    events = conditional_events(conditionals, tokens)
    candidates = []
    conditional_stack = []
    applied = 0
    brace_depth = 0
    index = 0
    while index < len(tokens):
        while applied <= index:
            brace_depth = apply_conditionals(
                events.get(applied, ()), brace_depth, conditional_stack
            )
            applied += 1
        token, line = tokens[index]
        if token == "{":
            brace_depth += 1
        elif token == "}":
            brace_depth = max(0, brace_depth - 1)
        elif (
            brace_depth == 0
            and token == "__P"
            and index > 0
            and IDENT.fullmatch(tokens[index - 1][0])
            and index + 1 < len(tokens)
            and tokens[index + 1][0] == "("
        ):
            name, name_line = tokens[index - 1]
            if name not in NOT_NAMES and name not in TYPE_WORDS:
                candidates.append(("declaration", name, name_line))
        elif (
            brace_depth == 0
            and IDENT.fullmatch(token)
            and token not in NOT_NAMES
            and token not in TYPE_WORDS
            and token != "__P"
            and index + 1 < len(tokens)
            and tokens[index + 1][0] == "("
        ):
            end = closing(tokens, index + 1)
            if end < 0:
                raise InventoryError(f"unbalanced declarator at line {line}")
            has_prefix = declaration_prefix(
                tokens, index
            ) or continuation_declaration_prefix(tokens, index)
            implicit_file_scope = statement_start(tokens, index) == index
            if (has_prefix or implicit_file_scope) and definition_follows(
                tokens, end + 1
            ):
                candidates.append(("symbol", token, line))
            else:
                after = following_attributes(tokens, end + 1)
                following = tokens[after][0] if 0 <= after < len(tokens) else ""
                if following in {",", ";"} and (has_prefix or implicit_file_scope):
                    candidates.append(("declaration", token, line))
            index = end
        index += 1
    return candidates


def read_source(path):
    try:
        return path.read_text(encoding="latin-1")
    except OSError as error:
        raise InventoryError(f"cannot read {path}: {error}") from error


def collect_repository(root):
    declarations = defaultdict(set)
    symbols = defaultdict(set)
    for path in source_files(root, DECLARATION_SUBTREES, ".h"):
        relative = path.relative_to(root).as_posix()
        for _kind, name, line in function_candidates(read_source(path)):
            declarations[name].add((relative, line))
    for path in source_files(root, SYMBOL_SUBTREES, ".c"):
        relative = path.relative_to(root).as_posix()
        for kind, name, line in function_candidates(read_source(path)):
            if kind == "symbol":
                symbols[name].add((relative, line))
    return declarations, symbols


def evidence_state(evidence):
    ordered = [
        {"line": line, "path": path}
        for path, line in sorted(evidence)
    ]
    return {
        "evidence": ordered,
        "state": "present" if ordered else "absent_from_scanned_scope",
    }


def unassessed_state():
    return {"evidence": [], "state": "not_assessed"}


def repository_evidence(name, declarations, symbols):
    return {
        "behavior": unassessed_state(),
        "declaration": evidence_state(declarations.get(name, ())),
        "linkage": unassessed_state(),
        "symbol": evidence_state(symbols.get(name, ())),
    }


def build_document(discobsd, bsd44):
    disc_declarations, disc_symbols = discobsd
    bsd_declarations, bsd_symbols = bsd44
    names = sorted(
        set(disc_declarations)
        | set(disc_symbols)
        | set(bsd_declarations)
        | set(bsd_symbols)
    )
    records = [
        {
            "bsd44": repository_evidence(name, bsd_declarations, bsd_symbols),
            "discobsd": repository_evidence(name, disc_declarations, disc_symbols),
            "name": name,
        }
        for name in names
    ]
    return {
        "apis": records,
        "methods": {
            "behavior": "not assessed by source discovery",
            "declaration": "lexical file-scope candidates in include and sys/sys",
            "linkage": "not assessed by source discovery",
            "symbol": "lexical function-definition candidates in lib/libc",
        },
        "scan_scope": {
            "declaration_subtrees": list(DECLARATION_SUBTREES),
            "symbol_subtrees": list(SYMBOL_SUBTREES),
        },
        "schema_version": 1,
    }


def main(argv=None):
    parser, arguments = parse_arguments(argv)
    try:
        discobsd_root = validate_root("discobsd", arguments.discobsd_root)
        bsd44_root = validate_root("bsd44", arguments.bsd44_root)
        document = build_document(
            collect_repository(discobsd_root),
            collect_repository(bsd44_root),
        )
    except InventoryError as error:
        parser.error(str(error))
    print(json.dumps(document, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
