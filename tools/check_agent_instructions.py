"""Validate the repository's two-file instruction graph, not client loading."""

import argparse
import os
import re
import stat
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
POLICY_FILES = {"AGENTS.md", "CLAUDE.md"}
WRAPPER = b"@AGENTS.md\n"


class PolicyError(Exception):
    """The selected instruction contents violate the repository contract."""


class InfrastructureError(Exception):
    """Git or the filesystem cannot supply the selected evidence."""


def git(root, *arguments):
    result = subprocess.run(["git", "-C", str(root), *arguments],
                            capture_output=True, check=False)
    if result.returncode:
        raise InfrastructureError(result.stderr.decode(errors="replace").strip())
    return result.stdout


def instruction_path(name):
    path = PurePosixPath(name.casefold())
    return path.name in {"agents.md", "agents.override.md", "claude.md", "claude.local.md"} or (
        path.name == ".claude" or
        any(parent == ".claude" and child == "rules"
            for parent, child in zip(path.parts, path.parts[1:]))
    )


def index_entries(root):
    entries = {}
    for record in git(root, "ls-files", "--stage", "-z").split(b"\0"):
        if not record:
            continue
        metadata, raw_name = record.split(b"\t", 1)
        name = os.fsdecode(raw_name)
        if not instruction_path(name):
            continue
        mode, object_id, stage = metadata.decode("ascii").split()
        if stage != "0":
            raise InfrastructureError(f"{name}: unresolved index stage {stage}")
        entries[name] = (mode, object_id)
    return entries


def visible_inline(contents):
    visible = []
    position = 0
    while position < len(contents):
        if contents[position] == "\\" and contents[position:position + 2] in {"\\`", "\\\\"}:
            visible.append(contents[position:position + 2])
            position += 2
            continue
        if contents[position] != "`":
            visible.append(contents[position])
            position += 1
            continue
        opening = re.match(r"`+", contents[position:])[0]
        end = position + len(opening)
        closing = re.search(rf"(?<!`)`{{{len(opening)}}}(?!`)", contents[end:])
        if closing:
            finish = end + closing.end()
            visible.append("".join("\n" if character == "\n" else " "
                                   for character in contents[position:finish]))
            position = finish
        else:
            visible.append(opening)
            position = end
    return "".join(visible)


def visible_markdown(contents):
    """Mask same-line backtick spans in the repository's literal-reference grammar."""
    return "".join(visible_inline(line) for line in contents.splitlines(keepends=True))


def active_imports(contents):
    for number, line in enumerate(visible_markdown(contents).splitlines(), 1):
        for token in re.finditer(r"(?<![\w@])@([^\s`]+)", line):
            yield number, token[1]


def check(root, staged=False):
    entries = index_entries(root)
    additional = set(entries) - POLICY_FILES
    if additional:
        raise PolicyError(f"instruction entry outside the allowed graph: {sorted(additional)}")
    blobs = {}
    for name in sorted(POLICY_FILES):
        if staged:
            if name not in entries:
                raise PolicyError(f"{name}: missing staged instruction file")
            mode, object_id = entries[name]
            if mode != "100644":
                raise PolicyError(f"{name}: expected regular mode 100644, found {mode}")
            blobs[name] = git(root, "cat-file", "blob", object_id)
        else:
            path = root / name
            try:
                mode = path.lstat().st_mode
            except FileNotFoundError as error:
                raise PolicyError(f"{name}: missing instruction file") from error
            if not stat.S_ISREG(mode) or mode & 0o111:
                raise PolicyError(f"{name}: expected a regular non-executable file")
            blobs[name] = path.read_bytes()
    if blobs["CLAUDE.md"] != WRAPPER:
        raise PolicyError("CLAUDE.md: expected exactly @AGENTS.md followed by one newline")
    try:
        canonical = blobs["AGENTS.md"].decode("utf-8")
    except UnicodeDecodeError as error:
        raise PolicyError("AGENTS.md: expected UTF-8 text") from error
    if not canonical.strip():
        raise PolicyError("AGENTS.md: canonical policy is empty")
    imports = list(active_imports(canonical))
    if imports:
        number, target = imports[0]
        raise PolicyError(f"AGENTS.md:{number}: forbidden active import @{target}; "
                          "only CLAUDE.md -> AGENTS.md is allowed; use a literal reference")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--staged", action="store_true")
    arguments = parser.parse_args()
    try:
        check(arguments.root.resolve(), arguments.staged)
    except PolicyError as error:
        print(f"FAIL agent-instructions: {error}", file=sys.stderr)
        return 1
    except (InfrastructureError, OSError) as error:
        print(f"ERROR agent-instructions: {error}", file=sys.stderr)
        return 2
    source = "index modes and blobs" if arguments.staged else "working-tree files"
    print(f"PASS agent-instructions: {source}; CLAUDE.md -> AGENTS.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
