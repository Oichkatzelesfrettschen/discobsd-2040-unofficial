"""Verify and refresh the NetBSD source files the tree imports.

tools/netbsd-imports.manifest names one NetBSD revision and, per imported
file, its SHA-256 and its path. An imported file keeps its NetBSD path in
this tree, so one path names both copies, and a directory that holds an
imported file holds only imported files: local adapters and backends live
elsewhere, which keeps every local change outside the hashed set.

A file the tree must correct carries a local patch. Its line adds
"patched <upstream-sha256> <patch>": the tree holds the patched bytes, the
first hash is theirs, the second is NetBSD's, and the patch under
tools/netbsd-patches turns NetBSD's bytes into the tree's. verify proves
the relation by reversing the patch; sync reapplies it and stops when it no
longer applies, so an upstream change to a patched file reaches review.

    verify            every listed file exists with its recorded hash, every
                      patch reverses the tree's bytes to the recorded NetBSD
                      bytes, and no unlisted file sits in an imported directory
    sync NETBSD_SRC   read every listed file (plus each --add PATH) from the
                      git objects of REVISION (default HEAD) in a NetBSD
                      checkout, apply recorded and --patch PATH=PATCH patches,
                      rewrite the manifest, and print each changed path
    selftest          calibrate verify and sync against passing and failing
                      trees

sync never reads NetBSD's working tree, so an uncommitted edit there cannot
enter under the revision's name.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

MANIFEST = PurePosixPath("tools/netbsd-imports.manifest")
PATCH_DIR = PurePosixPath("tools/netbsd-patches")
HEADER = (
    "# NetBSD files imported under tools/netbsd_imports.py; edit through its\n"
    "# sync command. Format: one 'revision SHA' line, then per file\n"
    "# '<sha256>  <path>', or '<sha256>  <path>  patched <upstream-sha256>\n"
    "# <patch>' for a file carrying a local patch. <path> is the same in\n"
    "# NetBSD and here.\n"
)


class ManifestError(Exception):
    """The manifest or the tree disagrees with the import contract."""


@dataclass
class Entry:
    digest: str
    upstream: str | None = None
    patch: str | None = None

    def render(self, path: str) -> str:
        if self.patch is None:
            return f"{self.digest}  {path}\n"
        return f"{self.digest}  {path}  patched {self.upstream} {self.patch}\n"


def checked_path(path: str, where: str) -> str:
    """Return PATH normalized, or raise when it is empty or leaves the tree."""
    pure = PurePosixPath(path)
    if not path or pure.is_absolute() or ".." in pure.parts or str(pure) == ".":
        raise ManifestError(f"{where}: {path!r} leaves the tree")
    return str(pure)


def is_digest(text: str) -> bool:
    return len(text) == 64 and all(c in "0123456789abcdef" for c in text.lower())


@dataclass
class Manifest:
    revision: str
    entries: dict[str, Entry]

    @classmethod
    def parse(cls, text: str) -> Manifest:
        revision = ""
        entries: dict[str, Entry] = {}
        for number, raw in enumerate(text.splitlines(), 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            where = f"line {number}"
            if fields[0] == "revision" and len(fields) == 2:
                if revision:
                    raise ManifestError(f"{where}: second revision line")
                revision = fields[1]
                continue
            if len(fields) == 2 and is_digest(fields[0]):
                entry = Entry(fields[0].lower())
            elif (
                len(fields) == 5
                and is_digest(fields[0])
                and fields[2] == "patched"
                and is_digest(fields[3])
            ):
                patch = checked_path(fields[4], where)
                if PurePosixPath(patch).parent != PATCH_DIR:
                    raise ManifestError(f"{where}: patch {patch} outside {PATCH_DIR}")
                entry = Entry(fields[0].lower(), fields[3].lower(), patch)
            else:
                raise ManifestError(
                    f"{where}: expected '<sha256>  <path>' or "
                    "'<sha256>  <path>  patched <sha256> <patch>'"
                )
            path = checked_path(fields[1], where)
            if path in entries:
                raise ManifestError(f"{where}: {path} listed twice")
            entries[path] = entry
        if not revision:
            raise ManifestError("no revision line")
        return cls(revision, entries)

    def render(self) -> str:
        lines = [HEADER, f"revision {self.revision}\n"]
        lines.extend(self.entries[p].render(p) for p in sorted(self.entries))
        return "".join(lines)


def digest_of(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load(root: Path) -> Manifest:
    path = root / MANIFEST
    if not path.is_file():
        raise ManifestError(f"{MANIFEST} is missing")
    return Manifest.parse(path.read_text(encoding="utf-8"))


def apply_patch(relative: str, data: bytes, patch: Path, reverse: bool) -> bytes:
    """Return DATA with PATCH applied (or reversed) to RELATIVE, or raise."""
    with tempfile.TemporaryDirectory() as scratch:
        target = Path(scratch) / relative
        target.parent.mkdir(parents=True)
        target.write_bytes(data)
        command = ["git", "apply", "--include", relative]
        if reverse:
            command.append("--reverse")
        command.append(str(patch.resolve()))
        result = subprocess.run(command, cwd=scratch, capture_output=True, text=True)
        if result.returncode != 0:
            direction = "reverse" if reverse else "apply"
            raise ManifestError(
                f"{relative}: {patch.name} does not {direction}: {result.stderr.strip()}"
            )
        return target.read_bytes()


def verify(root: Path) -> list[str]:
    """Return one message per violation; an empty list means the tree holds."""
    manifest = load(root)
    problems: list[str] = []
    directories: set[PurePosixPath] = set()
    for path, entry in sorted(manifest.entries.items()):
        directories.add(PurePosixPath(path).parent)
        file = root / path
        if not file.is_file():
            problems.append(f"{path}: listed but missing")
            continue
        data = file.read_bytes()
        if digest_of(data) != entry.digest:
            problems.append(f"{path}: content differs from the manifest hash")
            continue
        if entry.patch is None:
            continue
        patch = root / entry.patch
        if not patch.is_file():
            problems.append(f"{path}: patch {entry.patch} is missing")
            continue
        try:
            original = apply_patch(path, data, patch, reverse=True)
        except ManifestError as error:
            problems.append(str(error))
            continue
        if digest_of(original) != entry.upstream:
            problems.append(f"{path}: {entry.patch} does not reproduce the NetBSD bytes")
    for directory in sorted(directories):
        for file in sorted((root / directory).iterdir()):
            relative = str(directory / file.name)
            if file.is_file() and relative not in manifest.entries:
                problems.append(f"{relative}: unlisted file in an imported directory")
    return problems


def git(netbsd: Path, *args: str) -> bytes:
    result = subprocess.run(["git", "-C", str(netbsd), *args], capture_output=True)
    if result.returncode != 0:
        raise ManifestError(f"git {' '.join(args)}: {result.stderr.decode().strip()}")
    return result.stdout


def sync(
    root: Path,
    netbsd: Path,
    additions: list[str],
    patches: list[str],
    revision: str = "HEAD",
) -> list[str]:
    """Import the listed files from REVISION's objects and rewrite the manifest."""
    path = root / MANIFEST
    manifest = load(root) if path.is_file() else Manifest("", {})
    for addition in additions:
        manifest.entries.setdefault(checked_path(addition, "--add"), Entry(""))
    for request in patches:
        name, _, patch_file = request.partition("=")
        name = checked_path(name, "--patch")
        patch_file = checked_path(patch_file, "--patch")
        if PurePosixPath(patch_file).parent != PATCH_DIR:
            raise ManifestError(f"--patch: {patch_file} outside {PATCH_DIR}")
        if name not in manifest.entries:
            raise ManifestError(f"--patch: {name} is not imported")
        manifest.entries[name].patch = patch_file
    commit = git(netbsd, "rev-parse", "--verify", f"{revision}^{{commit}}").decode().strip()

    # Compute every result before writing, so a failure changes nothing.
    results: dict[str, tuple[bytes, Entry]] = {}
    for relative, entry in sorted(manifest.entries.items()):
        upstream = git(netbsd, "cat-file", "blob", f"{commit}:{relative}")
        data = upstream
        if entry.patch is not None:
            data = apply_patch(relative, upstream, root / entry.patch, reverse=False)
        results[relative] = (
            data,
            Entry(
                digest_of(data),
                digest_of(upstream) if entry.patch else None,
                entry.patch,
            ),
        )

    changed: list[str] = []
    for relative, (data, entry) in results.items():
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or target.read_bytes() != data:
            changed.append(relative)
        target.write_bytes(data)
        manifest.entries[relative] = entry
    manifest.revision = commit
    path.write_text(manifest.render(), encoding="utf-8")
    return changed


def selftest() -> None:
    def problems_of(root: Path) -> list[str]:
        try:
            return verify(root)
        except ManifestError as error:
            return [str(error)]

    def expect(root: Path, wanted: str | None, label: str) -> None:
        problems = problems_of(root)
        if wanted is None and problems:
            raise SystemExit(f"selftest {label}: passing tree rejected: {problems}")
        if wanted is not None and not any(wanted in p for p in problems):
            raise SystemExit(f"selftest {label}: wanted {wanted!r}, got {problems}")

    def expect_error(action: object, wanted: str, label: str) -> None:
        try:
            action()  # type: ignore[operator]
        except ManifestError as error:
            if wanted in str(error):
                return
            raise SystemExit(f"selftest {label}: wanted {wanted!r}, got {error}") from None
        raise SystemExit(f"selftest {label}: accepted")

    def run(cwd: Path, *command: str) -> None:
        subprocess.run(command, cwd=cwd, check=True, capture_output=True)

    rejected = 0
    with tempfile.TemporaryDirectory() as scratch:
        top = Path(scratch)
        # A NetBSD stand-in: one commit, then an uncommitted working-tree edit.
        netbsd = top / "netbsd"
        (netbsd / "sys" / "fs" / "demo").mkdir(parents=True)
        (netbsd / "sys" / "fs" / "demo" / "a.c").write_bytes(b"int a;\n")
        (netbsd / "sys" / "fs" / "demo" / "b.c").write_bytes(b"int b = 1;\n")
        run(netbsd, "git", "init", "-q")
        run(netbsd, "git", "add", ".")
        run(
            netbsd, "git", "-c", "user.name=t", "-c", "user.email=t@example.invalid",
            "commit", "-q", "-m", "base",
        )
        (netbsd / "sys" / "fs" / "demo" / "a.c").write_bytes(b"int uncommitted;\n")

        root = top / "tree"
        (root / PATCH_DIR).mkdir(parents=True)
        (root / PATCH_DIR / "b.c.patch").write_text(
            "--- a/sys/fs/demo/b.c\n+++ b/sys/fs/demo/b.c\n"
            "@@ -1 +1 @@\n-int b = 1;\n+int b = 2;\n",
            encoding="utf-8",
        )
        sync(
            root, netbsd, ["sys/fs/demo/a.c", "sys/fs/demo/b.c"],
            ["sys/fs/demo/b.c=tools/netbsd-patches/b.c.patch"],
        )
        if (root / "sys/fs/demo/a.c").read_bytes() != b"int a;\n":
            raise SystemExit("selftest sync: imported the working tree, not the commit")
        if (root / "sys/fs/demo/b.c").read_bytes() != b"int b = 2;\n":
            raise SystemExit("selftest sync: patch not applied")
        expect(root, None, "known-good")

        imported = root / "sys" / "fs" / "demo"
        cases = [
            ("a.c", b"int z;\n", "a.c: content differs", "altered byte"),
            ("b.c", b"int b = 3;\n", "b.c: content differs", "altered patched file"),
        ]
        for name, data, wanted, label in cases:
            saved = (imported / name).read_bytes()
            (imported / name).write_bytes(data)
            expect(root, wanted, label)
            (imported / name).write_bytes(saved)
            rejected += 1

        (imported / "a.c").unlink()
        expect(root, "a.c: listed but missing", "missing file")
        (imported / "a.c").write_bytes(b"int a;\n")
        rejected += 1

        (imported / "local.c").write_bytes(b"int local;\n")
        expect(root, "local.c: unlisted file", "unlisted file")
        (imported / "local.c").unlink()
        rejected += 1

        manifest_text = (root / MANIFEST).read_text(encoding="utf-8")
        # A patch that applies but whose reverse is not the recorded upstream.
        good_upstream = load(root).entries["sys/fs/demo/b.c"].upstream or ""
        (root / MANIFEST).write_text(
            manifest_text.replace(good_upstream, "0" * 64), encoding="utf-8"
        )
        expect(root, "does not reproduce the NetBSD bytes", "forged upstream hash")
        rejected += 1

        (root / MANIFEST).write_text(
            "\n".join(ln for ln in manifest_text.splitlines() if not ln.startswith("revision")),
            encoding="utf-8",
        )
        expect(root, "no revision line", "missing revision")
        (root / MANIFEST).write_text(manifest_text, encoding="utf-8")
        rejected += 1

        expect_error(
            lambda: sync(root, netbsd, ["../escape.c"], []), "leaves the tree", "--add escape"
        )
        expect_error(
            lambda: sync(root, netbsd, [], ["sys/fs/demo/a.c=/tmp/x.patch"]),
            "leaves the tree",
            "--patch escape",
        )
        rejected += 2
        if (top / "escape.c").exists():
            raise SystemExit("selftest --add escape: wrote outside the tree")
        expect(root, None, "unchanged after refused syncs")
    print(f"netbsd_imports: selftest passed (2 accepted, {rejected} rejected)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("verify")
    sync_parser = commands.add_parser("sync")
    sync_parser.add_argument("netbsd", type=Path)
    sync_parser.add_argument("--revision", default="HEAD")
    sync_parser.add_argument("--add", action="append", default=[], metavar="PATH")
    sync_parser.add_argument("--patch", action="append", default=[], metavar="PATH=PATCH")
    commands.add_parser("selftest")
    args = parser.parse_args(argv)

    try:
        if args.command == "selftest":
            selftest()
            return 0
        if args.command == "sync":
            for path in sync(
                args.root, args.netbsd.resolve(), args.add, args.patch, args.revision
            ):
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
