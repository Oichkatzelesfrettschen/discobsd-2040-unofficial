#!/usr/bin/env python3
"""Map 2.11BSD patch commits onto this tree.

DiscoBSD's userland descends from 2.11BSD through RetroBSD, so an upstream
patch either touches a file this tree still carries or it does not. For every
patch commit in a 2.11BSD repository, this reports the files it touches that
also exist here, and decides for each whether this tree already carries the
patch.

The decision compares this tree's file against the upstream parent and child
blobs of the patch commit. A copy closer to the child carries the patch; a
copy closer to the parent does not. Distance is the count of differing lines
after trailing-whitespace normalization, so tab and line-ending drift between
the trees does not decide the verdict. The parent distance is reported beside
the verdict because it is what makes the verdict usable: a file at distance
zero from the upstream parent is that file verbatim and the patch applies to
it, while a file hundreds of lines away was rewritten here and the upstream
hunks are advisory at best.

The 2.11BSD repository is named by --bsd or the BSD211 environment variable.
It must be a git checkout whose history carries one commit per patch; the
base revision is the tape the history starts from.

    ${PYTHON} tools/analysis/bsd211_patch_scope.py --bsd DIR > scope.json
    ${PYTHON} tools/analysis/bsd211_patch_scope.py --bsd DIR --format table
"""

import argparse
import difflib
import json
import os
import re
import subprocess
import sys

# Distance from the upstream pre-patch text, in differing lines, below which
# this tree's copy is close enough that an upstream hunk is directly usable.
APPLICABLE_MAX = 40

# Above this, the file was rewritten here and upstream hunks do not apply.
PARTIAL_MAX = 150


def git(repo, *args):
    return subprocess.run(
        ["git", "-C", repo, *args],
        capture_output=True, text=True, errors="replace", check=False,
    )


def blob(repo, rev, path):
    result = git(repo, "show", f"{rev}:{path}")
    return None if result.returncode else result.stdout


def normalize(text):
    return [line.rstrip() for line in text.splitlines()]


def distance(left, right):
    """Differing lines between two texts, ignoring trailing whitespace."""
    a, b = normalize(left), normalize(right)
    matcher = difflib.SequenceMatcher(None, a, b, autojunk=False)
    common = sum(block.size for block in matcher.get_matching_blocks())
    return len(a) + len(b) - 2 * common


def patch_number(subject):
    match = re.search(r"patch (\d+)", subject) or re.search(r"\(#(\d+)\)", subject)
    return int(match.group(1)) if match else None


def scope(bsd, tree, base):
    results = []
    log = git(bsd, "log", "--reverse", "--format=%H%x00%s", f"{base}..HEAD")
    for line in log.stdout.splitlines():
        if not line.strip():
            continue
        sha, subject = line.split("\0", 1)
        touched = git(bsd, "diff", "--name-only", f"{sha}^", sha).stdout.split()
        carried, missing, undecided = [], [], []
        shared = 0
        for path in touched:
            local = os.path.join(tree, path)
            if not os.path.isfile(local):
                continue
            shared += 1
            with open(local, encoding="utf-8", errors="replace") as handle:
                ours = handle.read()
            before, after = blob(bsd, f"{sha}^", path), blob(bsd, sha, path)
            if before is None or after is None:
                undecided.append([path, "added or removed upstream"])
                continue
            d_before, d_after = distance(ours, before), distance(ours, after)
            if d_after < d_before:
                carried.append([path, d_before, d_after])
            elif d_before < d_after:
                missing.append([path, d_before, d_after])
            else:
                undecided.append([path, f"equidistant at {d_before}"])
        results.append({
            "patch": patch_number(subject),
            "sha": sha[:7],
            "subject": subject,
            "upstream_files": len(touched),
            "shared_files": shared,
            "carried": carried,
            "missing": missing,
            "undecided": undecided,
        })
    return results


def applicability(d_before):
    if d_before <= APPLICABLE_MAX:
        return "applicable"
    if d_before <= PARTIAL_MAX:
        return "partial"
    return "rewritten"


def as_table(results, out):
    print(f"{'patch':>6} {'upstream':>8} {'shared':>6} {'carried':>7} "
          f"{'missing':>7} {'undec':>5}  subject", file=out)
    for row in results:
        if row["shared_files"] == 0:
            continue
        label = str(row["patch"]) if row["patch"] else row["subject"][:24]
        print(f"{label:>6} {row['upstream_files']:>8} {row['shared_files']:>6} "
              f"{len(row['carried']):>7} {len(row['missing']):>7} "
              f"{len(row['undecided']):>5}  {row['subject'][:64]}", file=out)

    print("\nUpstream fixes this tree does not carry, by applicability:",
          file=out)
    rows = []
    for row in results:
        label = str(row["patch"]) if row["patch"] else row["subject"][:24]
        for path, d_before, d_after in row["missing"]:
            rows.append((d_before, d_after, label, path))
    rows.sort(key=lambda entry: entry[0])
    for d_before, d_after, label, path in rows:
        print(f"{applicability(d_before):>10}  parent={d_before:<5} "
              f"child={d_after:<5} patch={label:<26} {path}", file=out)

    quiet = sum(1 for row in results if row["shared_files"] == 0)
    print(f"\n{quiet} of {len(results)} upstream commits touch no file this "
          f"tree carries.", file=out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsd", default=os.environ.get("BSD211"),
                        help="2.11BSD git checkout (or set BSD211)")
    parser.add_argument("--tree", default=os.getcwd(),
                        help="this repository's root")
    parser.add_argument("--base", default="",
                        help="base revision of the 2.11BSD history; the "
                             "default is its root commit")
    parser.add_argument("--format", choices=("json", "table"), default="json")
    args = parser.parse_args()

    if not args.bsd:
        parser.error("no 2.11BSD checkout: pass --bsd or set BSD211")
    if not os.path.isdir(os.path.join(args.bsd, ".git")):
        parser.error(f"{args.bsd} is not a git checkout")

    base = args.base
    if not base:
        roots = git(args.bsd, "rev-list", "--max-parents=0", "HEAD")
        base = roots.stdout.split()[0]

    results = scope(args.bsd, args.tree, base)
    if args.format == "json":
        json.dump(results, sys.stdout, indent=1)
        sys.stdout.write("\n")
    else:
        as_table(results, sys.stdout)


if __name__ == "__main__":
    main()
