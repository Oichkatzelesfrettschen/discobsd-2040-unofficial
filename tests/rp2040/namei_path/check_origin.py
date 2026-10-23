#!/usr/bin/env python3
"""Verify the finite set of kernel pathname-origin transitions."""

from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path

EXPECTED_CALLS = {
    "sys/kern/exec_script.c": [
        "NAMEI_INIT_KERNEL(ndp,LOOKUP,FOLLOW,cp)",
    ],
    "sys/kern/kern_exec.c": [
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
    ],
    "sys/kern/kern_sig.c": [
        "NAMEI_INIT_KERNEL(ndp,CREATE,FOLLOW,name)",
    ],
    "sys/kern/ufs_mount.c": [
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->freg)",
    ],
    "sys/kern/ufs_syscalls.c": [
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,fname)",
        "NAMEI_INIT_USER(ndp,CREATE,NOFOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->target)",
        "NAMEI_INIT_USER(ndp,CREATE,NOFOLLOW,uap->linkname)",
        "NAMEI_INIT_USER(ndp,CREATE,NOFOLLOW,uap->linkname)",
        "NAMEI_INIT_USER(ndp,DELETE,LOCKPARENT,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,follow,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,NOFOLLOW,uap->name)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,NOFOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
        "NAMEI_INIT_USER(ndp,DELETE,LOCKPARENT,uap->from)",
        "NAMEI_INIT_USER(ndp,CREATE,LOCKPARENT|NOCACHE,uap->to)",
        "NAMEI_INIT_USER(ndp,DELETE,LOCKPARENT,uap->from)",
        "NAMEI_INIT_USER(ndp,CREATE,NOFOLLOW,uap->name)",
        "NAMEI_INIT_USER(ndp,LOOKUP,NOCACHE,uap->name)",
        "NAMEI_INIT_USER(ndp,DELETE,LOCKPARENT,uap->name)",
    ],
    "sys/kern/ufs_syscalls2.c": [
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->path)",
        "NAMEI_INIT_USER(ndp,LOOKUP,FOLLOW,uap->fname)",
    ],
    "sys/kern/vfs_vnops.c": [
        "NAMEI_RESET_OPERATION(ndp,CREATE,(fmode&O_EXCL)?NOFOLLOW:FOLLOW)",
        "NAMEI_RESET_OPERATION(ndp,LOOKUP,FOLLOW)",
    ],
}

# The conditional vn_open reset expresses separate O_EXCL and non-O_EXCL
# transitions through one macro invocation so the Thumb-1 build shares the
# origin-preservation sequence.
CONDITIONAL_TRANSITION_CASES = 1

CALL_PATTERN = re.compile(
    r"\b(?:NAMEI_INIT_USER|NAMEI_INIT_KERNEL|NAMEI_RESET_OPERATION)\s*"
    r"\([^;]*\)"
)
DIRECT_WRITE_PATTERN = re.compile(
    r"(?:->|\.)\s*ni_(?:dirp|nameiop)\s*"
    r"(?:=(?!=)|[+\-*/%&^|]=|<<=|>>=)"
)
LEGACY_INIT_PATTERN = re.compile(r"\bNDINIT(?:_KERNEL)?\s*\(")


def normalized_calls(source_text: str) -> Counter[str]:
    return Counter(re.sub(r"\s+", "", call) for call in CALL_PATTERN.findall(source_text))


def load_kernel_sources(root: Path) -> dict[str, str]:
    sources: dict[str, str] = {}
    for source_path in sorted((root / "sys/kern").glob("*.c")):
        relative_path = source_path.relative_to(root).as_posix()
        sources[relative_path] = source_path.read_text(encoding="utf-8")
    return sources


def validate_sources(sources: dict[str, str]) -> list[str]:
    errors: list[str] = []
    observed_calls: dict[str, Counter[str]] = {}

    for relative_path, source_text in sources.items():
        direct_writes = DIRECT_WRITE_PATTERN.findall(source_text)
        if direct_writes:
            errors.append(
                f"{relative_path}: {len(direct_writes)} direct nameidata origin writes"
            )
        legacy_initializers = LEGACY_INIT_PATTERN.findall(source_text)
        if legacy_initializers:
            errors.append(
                f"{relative_path}: {len(legacy_initializers)} ambiguous NDINIT calls"
            )

        call_counts = normalized_calls(source_text)
        if call_counts:
            observed_calls[relative_path] = call_counts

    all_paths = sorted(set(EXPECTED_CALLS) | set(observed_calls))
    for relative_path in all_paths:
        expected = Counter(EXPECTED_CALLS.get(relative_path, []))
        observed = observed_calls.get(relative_path, Counter())
        if observed != expected:
            missing = sorted((expected - observed).elements())
            unexpected = sorted((observed - expected).elements())
            errors.append(
                f"{relative_path}: missing origin calls {missing}; "
                f"unexpected origin calls {unexpected}"
            )
    return errors


def require_rejection(label: str, sources: dict[str, str]) -> None:
    if not validate_sources(sources):
        raise RuntimeError(f"{label}: malformed caller inventory was accepted")


def require_direct_write_rejection(
    label: str, caller_path: str, sources: dict[str, str]
) -> None:
    expected_error = f"{caller_path}: 1 direct nameidata origin writes"
    errors = validate_sources(sources)
    if errors != [expected_error]:
        raise RuntimeError(
            f"{label}: expected only {expected_error!r}, observed {errors!r}"
        )


def run_self_tests(clean_sources: dict[str, str]) -> int:
    rejected = 0

    wrong_origin_sources = dict(clean_sources)
    caller_path = "sys/kern/kern_exec.c"
    wrong_origin_sources[caller_path] = wrong_origin_sources[caller_path].replace(
        "NAMEI_INIT_USER", "NAMEI_INIT_KERNEL", 1
    )
    require_rejection("wrong-origin", wrong_origin_sources)
    rejected += 1

    wrong_path_sources = dict(clean_sources)
    caller_path = "sys/kern/ufs_syscalls.c"
    wrong_path_sources[caller_path] = wrong_path_sources[caller_path].replace(
        "NAMEI_INIT_USER (ndp, LOOKUP, FOLLOW, uap->target);",
        "NAMEI_INIT_USER (ndp, LOOKUP, FOLLOW, uap->linkname);",
        1,
    )
    require_rejection("wrong-path", wrong_path_sources)
    rejected += 1

    direct_write_sources = dict(clean_sources)
    caller_path = "sys/kern/vfs_vnops.c"
    direct_write_sources[caller_path] = direct_write_sources[caller_path].replace(
        "NAMEI_RESET_OPERATION(ndp, LOOKUP, FOLLOW);",
        "NAMEI_RESET_OPERATION(ndp, LOOKUP, FOLLOW);\n"
        "        ndp->ni_nameiop = CREATE;",
        1,
    )
    if normalized_calls(direct_write_sources[caller_path]) != normalized_calls(
        clean_sources[caller_path]
    ):
        raise RuntimeError("direct-write mutation changed the caller inventory")
    require_direct_write_rejection(
        "direct-origin-write", caller_path, direct_write_sources
    )
    rejected += 1

    compound_write_sources = dict(clean_sources)
    caller_path = "sys/kern/vfs_vnops.c"
    compound_write_sources[caller_path] = compound_write_sources[caller_path].replace(
        "NAMEI_RESET_OPERATION(ndp, CREATE,\n"
        "            (fmode & O_EXCL) ? NOFOLLOW : FOLLOW);",
        "NAMEI_RESET_OPERATION(ndp, CREATE,\n"
        "            (fmode & O_EXCL) ? NOFOLLOW : FOLLOW);\n"
        "        ndp->ni_nameiop &= ~NI_USERPATH;",
        1,
    )
    if normalized_calls(compound_write_sources[caller_path]) != normalized_calls(
        clean_sources[caller_path]
    ):
        raise RuntimeError("compound-write mutation changed the caller inventory")
    require_direct_write_rejection(
        "compound-origin-write", caller_path, compound_write_sources
    )
    rejected += 1

    unexpected_caller_sources = dict(clean_sources)
    caller_path = "sys/kern/init_main.c"
    unexpected_caller_sources[caller_path] += (
        "\nNAMEI_INIT_USER(ndp, LOOKUP, FOLLOW, pathname);\n"
    )
    require_rejection("unexpected-caller", unexpected_caller_sources)
    rejected += 1

    return rejected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    sources = load_kernel_sources(root)
    rejected_mutations = run_self_tests(sources)
    errors = validate_sources(sources)
    if errors:
        for error in errors:
            print(f"FAIL namei path origin: {error}")
        return 1

    call_count = (
        sum(len(calls) for calls in EXPECTED_CALLS.values())
        + CONDITIONAL_TRANSITION_CASES
    )
    print(
        "PASS namei path origin: "
        f"{call_count} transitions, {rejected_mutations} mutations rejected"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
