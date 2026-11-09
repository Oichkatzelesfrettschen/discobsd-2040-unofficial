"""Verify and refresh the NetBSD source files the tree imports verbatim.

tools/netbsd-imports.manifest names one NetBSD revision and, per imported
file, its SHA-256 and its path. An imported file keeps its NetBSD path in
this tree, so one path names both copies, and a directory that holds an
imported file holds only imported files: local adapters and backends live
elsewhere, which keeps every local change outside the hashed set.

    verify            every listed file exists with its recorded hash, and
                      no unlisted file sits in an imported directory
    sync NETBSD_SRC   copy every listed file (plus each --add PATH) from a
                      NetBSD checkout, rewrite the hashes and the revision,
                      and print the paths whose content changed
    selftest          calibrate verify against a passing tree and against
                      an altered byte, a missing file, an unlisted file and
                      a missing revision line
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

MANIFEST = PurePosixPath("tools/netbsd-imports.manifest")
HEADER = (
    "# NetBSD files imported verbatim. Maintained by tools/netbsd_imports.py;\n"
    "# edit through its sync command. Format: one 'revision SHA' line, then\n"
    "# '<sha256>  <path>' per file, where <path> is the same in NetBSD and here.\n"
)


class ManifestError(Exception):
    """The manifest or the tree disagrees with the import contract."""


@dataclass
class Manifest:
    revision: str
    entries: dict[str, str]  # path -> sha256

    @classmethod
    def parse(cls, text: str) -> Manifest:
        revision = ""
        entries: dict[str, str] = {}
        for number, raw in enumerate(text.splitlines(), 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if fields[0] == "revision" and len(fields) == 2:
                if revision:
                    raise ManifestError(f"line {number}: second revision line")
                revision = fields[1]
                continue
            if len(fields) != 2 or len(fields[0]) != 64:
                raise ManifestError(f"line {number}: expected '<sha256>  <path>'")
            digest, path = fields
            if path in entries:
                raise ManifestError(f"line {number}: {path} listed twice")
            pure = PurePosixPath(path)
            if pure.is_absolute() or ".." in pure.parts:
                raise ManifestError(f"line {number}: {path} leaves the tree")
            entries[path] = digest.lower()
        if not revision:
            raise ManifestError("no revision line")
        return cls(revision, entries)

    def render(self) -> str:
        lines = [HEADER, f"revision {self.revision}\n"]
        lines.extend(f"{self.entries[p]}  {p}\n" for p in sorted(self.entries))
        return "".join(lines)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(root: Path) -> Manifest:
    path = root / MANIFEST
    if not path.is_file():
        raise ManifestError(f"{MANIFEST} is missing")
    return Manifest.parse(path.read_text(encoding="utf-8"))


def verify(root: Path) -> list[str]:
    """Return one message per violation; an empty list means the tree holds."""
    manifest = load(root)
    problems: list[str] = []
    directories: set[PurePosixPath] = set()
    for path, digest in sorted(manifest.entries.items()):
        directories.add(PurePosixPath(path).parent)
        file = root / path
        if not file.is_file():
            problems.append(f"{path}: listed but missing")
        elif sha256(file) != digest:
            problems.append(f"{path}: content differs from the manifest hash")
    for directory in sorted(directories):
        for file in sorted((root / directory).iterdir()):
            relative = str(directory / file.name)
            if file.is_file() and relative not in manifest.entries:
                problems.append(f"{relative}: unlisted file in an imported directory")
    return problems


def netbsd_revision(netbsd: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(netbsd), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def sync(root: Path, netbsd: Path, additions: list[str]) -> list[str]:
    """Copy the listed files from NETBSD and rewrite the manifest."""
    path = root / MANIFEST
    if path.is_file():
        manifest = load(root)
    else:
        manifest = Manifest("", {})
    for addition in additions:
        manifest.entries.setdefault(str(PurePosixPath(addition)), "")
    changed: list[str] = []
    for relative in sorted(manifest.entries):
        source = netbsd / relative
        if not source.is_file():
            raise ManifestError(f"{relative}: absent from {netbsd}")
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        digest = sha256(source)
        if not target.is_file() or sha256(target) != digest:
            changed.append(relative)
        shutil.copyfile(source, target)
        manifest.entries[relative] = digest
    manifest.revision = netbsd_revision(netbsd)
    path.write_text(manifest.render(), encoding="utf-8")
    return changed


def selftest() -> None:
    def expect(root: Path, wanted: str | None, label: str) -> None:
        try:
            problems = verify(root)
        except ManifestError as error:
            problems = [str(error)]
        if wanted is None and problems:
            raise SystemExit(f"selftest {label}: passing tree rejected: {problems}")
        if wanted is not None and not any(wanted in p for p in problems):
            raise SystemExit(f"selftest {label}: wanted {wanted!r}, got {problems}")

    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / "tools").mkdir()
        imported = root / "sys" / "fs" / "demo"
        imported.mkdir(parents=True)
        (imported / "a.c").write_bytes(b"int a;\n")
        (imported / "b.h").write_bytes(b"#define B 1\n")
        manifest = Manifest(
            "0" * 40,
            {
                "sys/fs/demo/a.c": sha256(imported / "a.c"),
                "sys/fs/demo/b.h": sha256(imported / "b.h"),
            },
        )
        (root / MANIFEST).write_text(manifest.render(), encoding="utf-8")
        expect(root, None, "known-good")

        (imported / "a.c").write_bytes(b"int b;\n")
        expect(root, "a.c: content differs", "altered byte")
        (imported / "a.c").write_bytes(b"int a;\n")

        (imported / "b.h").unlink()
        expect(root, "b.h: listed but missing", "missing file")
        (imported / "b.h").write_bytes(b"#define B 1\n")

        (imported / "local.c").write_bytes(b"int local;\n")
        expect(root, "local.c: unlisted file", "unlisted file")
        (imported / "local.c").unlink()

        without_revision = "\n".join(
            line for line in manifest.render().splitlines() if not line.startswith("revision")
        )
        (root / MANIFEST).write_text(without_revision, encoding="utf-8")
        expect(root, "no revision line", "missing revision")
    print("netbsd_imports: selftest passed (1 accepted, 4 rejected)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("verify")
    sync_parser = commands.add_parser("sync")
    sync_parser.add_argument("netbsd", type=Path)
    sync_parser.add_argument("--add", action="append", default=[], metavar="PATH")
    commands.add_parser("selftest")
    args = parser.parse_args(argv)

    try:
        if args.command == "selftest":
            selftest()
            return 0
        if args.command == "sync":
            for path in sync(args.root, args.netbsd.resolve(), args.add):
                print(f"changed {path}")
            return 0
        problems = verify(args.root)
    except ManifestError as error:
        print(f"netbsd_imports: {error}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"netbsd_imports: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"netbsd_imports: {len(load(args.root).entries)} files match the manifest")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
