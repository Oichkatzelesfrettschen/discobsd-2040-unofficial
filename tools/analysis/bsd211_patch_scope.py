"""Inventory pinned 2.11BSD textual candidates; semantic review is separate.

Distances compare complete files after stripping trailing whitespace. They
establish neither patch ancestry, behavioral coverage nor transplant safety.
Explicit path/symbol hints retain relocated implementations in the inventory.
"""

import argparse
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys
from functools import lru_cache
from pathlib import Path, PurePosixPath

DEFAULT_RELOCATIONS = Path(__file__).with_name("bsd211-relocations.json")


class GitError(Exception):
    """Git could not establish the requested source evidence."""


def git(repo, *arguments):
    result = subprocess.run(["git", "-C", str(repo), *arguments],
                            capture_output=True, check=False)
    if result.returncode:
        diagnostic = result.stderr.decode(errors="replace").strip()
        raise GitError(f"git {arguments[0]}: {diagnostic or 'command failed'}")
    return result.stdout


def resolve(repo, revision):
    return git(repo, "rev-parse", "--verify", "--end-of-options",
               revision + "^{commit}").decode("ascii").strip()


@lru_cache(maxsize=4)
def tree_entries(repo, revision):
    entries = {}
    for record in git(repo, "ls-tree", "-r", "--full-tree", "-z", revision).split(b"\0"):
        if record:
            metadata, name = record.split(b"\t", 1)
            mode, kind, object_id = metadata.decode("ascii").split()
            entries[os.fsdecode(name)] = {"mode": mode, "kind": kind, "object": object_id}
    return entries


@lru_cache(maxsize=32)
def blob(repo, object_id):
    return git(repo, "cat-file", "blob", object_id)


def distance(left, right):
    """Count differing lines, ignoring trailing whitespace, without a verdict."""
    left_lines = [line.rstrip() for line in left.splitlines()]
    right_lines = [line.rstrip() for line in right.splitlines()]
    matcher = difflib.SequenceMatcher(None, left_lines, right_lines, autojunk=False)
    common = sum(block.size for block in matcher.get_matching_blocks())
    return len(left_lines) + len(right_lines) - 2 * common


def patch_number(subject):
    match = re.search(r"\bpatch\s+#?(\d+)\b|\(#(\d+)\)|^\s*(\d+)[.:) -]", subject, re.I)
    return next(int(group) for group in match.groups() if group) if match else None


def read_relocations(path):
    contents = path.read_bytes()
    rows = json.loads(contents)
    if not isinstance(rows, list):
        raise ValueError("relocations must be a list")
    mappings = {}
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"donor_path", "recipient_path", "symbols"}:
            raise ValueError("relocations require donor_path, recipient_path and symbols")
        for key in ("donor_path", "recipient_path"):
            name = row[key]
            if not isinstance(name, str) or not name or "\0" in name or (
                PurePosixPath(name).is_absolute() or ".." in PurePosixPath(name).parts
            ):
                raise ValueError(f"invalid relocation {key}")
        if not isinstance(row["symbols"], list) or any(
            not isinstance(symbol, dict) or set(symbol) != {"donor", "recipient"} or any(
                not isinstance(value, str) or not re.fullmatch(r"[A-Za-z_]\w*", value)
                for value in symbol.values()
            ) for symbol in row["symbols"]
        ):
            raise ValueError("relocation symbols require donor/recipient identifier hints")
        mappings.setdefault(row["donor_path"], []).append(row)
    for rows_for_path in mappings.values():
        paths = [row["recipient_path"] for row in rows_for_path]
        if len(paths) != len(set(paths)):
            raise ValueError("duplicate relocation destination")
    return mappings, hashlib.sha256(contents).hexdigest()


def textual_candidate(donor, recipient, donor_path, mapping, before, after, local):
    row = {
        "donor_path": donor_path, "recipient_path": mapping["recipient_path"],
        "mapping": "explicit" if mapping["explicit"] else "same-path",
        "symbol_hints": mapping["symbols"],
        "donor_parent_entry": before, "donor_child_entry": after, "recipient_entry": local,
        "textual_relation": "uncompared", "parent_distance": None, "child_distance": None,
    }
    if local is None:
        row["reason"] = "recipient path absent; relocation or replacement review required"
        return row
    if before is None or after is None:
        row["reason"] = "donor addition or deletion"
        return row
    entries = (before, after, local)
    if any(entry["kind"] != "blob" or entry["mode"] not in {"100644", "100755"}
           for entry in entries):
        row["reason"] = "nonregular tree entry"
        return row
    contents = [blob(donor, before["object"]), blob(donor, after["object"]),
                blob(recipient, local["object"])]
    try:
        if any(b"\0" in content for content in contents):
            raise UnicodeError("binary content")
        parent_text, child_text, recipient_text = [content.decode("utf-8") for content in contents]
    except UnicodeError:
        row["reason"] = "binary or non-UTF-8 content"
        return row
    parent_distance = distance(recipient_text, parent_text)
    child_distance = distance(recipient_text, child_text)
    row.update(parent_distance=parent_distance, child_distance=child_distance,
               textual_relation="closer-to-parent" if parent_distance < child_distance else
               "closer-to-child" if child_distance < parent_distance else "equidistant")
    return row


def scope(bsd, tree, base=None, donor_ref="HEAD", recipient_ref="HEAD", relocations=None):
    donor = str(Path(bsd).resolve())
    recipient = str(Path(tree).resolve())
    donor_commit = resolve(donor, donor_ref)
    recipient_commit = resolve(recipient, recipient_ref)
    if base is None:
        roots = git(donor, "rev-list", "--max-parents=0", donor_commit).decode().splitlines()
        if len(roots) != 1:
            raise ValueError("donor has multiple roots; select an explicit --base")
        base_commit = roots[0]
    else:
        base_commit = resolve(donor, base)
    ancestors = set(git(donor, "rev-list", donor_commit).decode().splitlines())
    if base_commit not in ancestors:
        raise ValueError("base is outside the pinned donor ancestry")
    mappings, mapping_hash = read_relocations(relocations or DEFAULT_RELOCATIONS)
    recipient_entries = tree_entries(recipient, recipient_commit)
    commits = []
    history = git(donor, "rev-list", "--reverse", "--topo-order", "--parents",
                  f"{base_commit}..{donor_commit}")
    for record in history.decode("ascii").splitlines():
        commit, *parents = record.split()
        subject = git(donor, "show", "-s", "--format=%s", commit).decode(errors="replace").rstrip()
        item = {"commit": commit, "parents": parents, "subject": subject,
                "patch": patch_number(subject), "merge": len(parents) > 1, "comparisons": []}
        # Each merge parent has a distinct delta. Repeated textual changes
        # across those comparisons remain separate from semantic fix counts.
        for parent in parents or [None]:
            before_entries = tree_entries(donor, parent) if parent else {}
            after_entries = tree_entries(donor, commit)
            if parent:
                touched = git(donor, "diff", "--name-only", "--no-renames",
                              "--ignore-submodules=none", "-z", parent, commit)
                paths = [os.fsdecode(path) for path in touched.split(b"\0") if path]
            else:
                paths = sorted(after_entries)
            candidates = []
            for path in paths:
                destinations = mappings.get(path, [{"recipient_path": path, "symbols": []}])
                for destination in destinations:
                    candidates.append(textual_candidate(
                        donor, recipient, path,
                        {**destination, "explicit": path in mappings},
                        before_entries.get(path), after_entries.get(path),
                        recipient_entries.get(destination["recipient_path"]),
                    ))
            item["comparisons"].append({"parent": parent, "touched_paths": len(paths),
                                        "textual_candidates": candidates})
        commits.append(item)
    return {
        "schema": 2, "donor_commit": donor_commit, "base_commit": base_commit,
        "recipient_commit": recipient_commit, "relocations_sha256": mapping_hash,
        "method": "whole-file differing lines after trailing-whitespace stripping",
        "limits": "textual candidates only; semantic coverage, ancestry and safety require review",
        "commits": commits,
    }


def as_table(report, output):
    print(f"donor={report['donor_commit']} base={report['base_commit']}\n"
          f"recipient={report['recipient_commit']}\n{report['limits']}", file=output)
    for commit in report["commits"]:
        for comparison in commit["comparisons"]:
            for row in comparison["textual_candidates"]:
                print(f"patch={commit['patch']} commit={commit['commit']} "
                      f"parent={comparison['parent']} {row['textual_relation']} "
                      f"distances={row['parent_distance']},{row['child_distance']} "
                      f"{json.dumps(row['donor_path'])} -> {json.dumps(row['recipient_path'])} "
                      f"{row.get('reason', '')}", file=output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsd", default=os.environ.get("BSD211"), help="donor Git checkout")
    parser.add_argument("--tree", default=os.getcwd(), help="recipient Git checkout")
    parser.add_argument("--base", help="donor boundary; default requires one root")
    parser.add_argument("--donor-ref", default="HEAD")
    parser.add_argument("--recipient-ref", default="HEAD")
    parser.add_argument("--relocations", type=Path, default=DEFAULT_RELOCATIONS)
    parser.add_argument("--format", choices=("json", "table"), default="json")
    args = parser.parse_args()
    if not args.bsd:
        parser.error("pass --bsd or set BSD211")
    try:
        report = scope(args.bsd, args.tree, args.base, args.donor_ref,
                       args.recipient_ref, args.relocations)
    except (GitError, OSError, ValueError) as error:
        print(f"ERROR patch-scope: {error}", file=sys.stderr)
        return 2
    if args.format == "json":
        json.dump(report, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        as_table(report, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
