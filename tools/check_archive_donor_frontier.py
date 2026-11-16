"""Validate content-identified archive candidates without executing or extracting them."""

import argparse
import hashlib
import json
import re
import stat
import subprocess
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

FRONTIER = "docs/research/posix-archive-donor-frontier.json"
ROOTS = (
    "extracted/archives/bsd",
    "extracted/archives/att",
    "extracted/archives/osf",
    "extracted/archives/xinu",
    "extracted/collections/schily/schily-2024-03-21",
    "extracted/collections/mach",
    "sources/sun/os/opensolaris/schillix/collection",
    "sources/sun/os/sunos",
    "sources/sun/os/solaris",
    "archives/mach",
    "archives/minix",
    "archives/research",
    "archives/sel4/SeL4-src",
)
CLAIM = (
    "Inventory and selected source inspection; semantic closure and target "
    "resource acceptance remain open."
)
MEMBER_LIMIT = 8 * 1024 * 1024


class FrontierError(ValueError):
    """Report an invalid identity, evidence claim or open frontier."""


def require(condition, message):
    if not condition:
        raise FrontierError(message)


def digest(value):
    return isinstance(value, str) and re.fullmatch(r"[a-f0-9]{64}", value) and value != "0" * 64


def safe_path(value):
    return (
        isinstance(value, str)
        and bool(value)
        and "\0" not in value
        and not PurePosixPath(value).is_absolute()
        and all(part not in {"", ".", ".."} for part in value.split("/"))
    )


def validate(document):
    require(
        isinstance(document, dict)
        and set(document) == {"schema_version", "claim", "roots", "candidate_order", "candidates"},
        "frontier fields differ",
    )
    require(
        type(document["schema_version"]) is int and document["schema_version"] == 1,
        "unsupported frontier schema",
    )
    require(document["claim"] == CLAIM, "frontier claim drift")
    roots = document["roots"]
    require(isinstance(roots, dict) and tuple(roots) == ROOTS, "archive root denominator drift")
    for name, record in roots.items():
        require(
            isinstance(record, dict)
            and set(record)
            == {"regular_files", "symlinks", "other", "bytes", "pathset_sha256", "next_experiment"},
            "root fields differ",
        )
        for field in ("regular_files", "symlinks", "other", "bytes"):
            require(type(record[field]) is int and record[field] >= 0, "invalid inventory count")
        require(digest(record["pathset_sha256"]), "invalid pathset digest")
        require(
            isinstance(record["next_experiment"], str) and record["next_experiment"].strip(),
            f"missing root experiment: {name}",
        )
    candidates = document["candidates"]
    require(isinstance(candidates, list) and candidates, "missing archive candidates")
    require(
        all(isinstance(candidate, dict) and "id" in candidate for candidate in candidates),
        "invalid candidate record",
    )
    identifiers = [candidate["id"] for candidate in candidates]
    require(
        all(
            isinstance(identifier, str) and re.fullmatch(r"[a-z][a-z0-9-]*", identifier)
            for identifier in identifiers
        ),
        "invalid candidate identifier",
    )
    require(
        identifiers == document["candidate_order"] and len(set(identifiers)) == len(identifiers),
        "candidate denominator drift",
    )
    covered = set()
    identities = set()
    for candidate in candidates:
        require(
            set(candidate)
            == {
                "id",
                "root",
                "path",
                "format",
                "member",
                "sha256",
                "container_sha256",
                "license_observation",
                "use",
                "family",
                "invariant",
                "conflict",
                "experiment",
                "evidence",
            },
            "candidate fields differ",
        )
        require(
            isinstance(candidate["root"], str) and candidate["root"] in roots,
            "unknown archive root",
        )
        covered.add(candidate["root"])
        require(safe_path(candidate["path"]), "unsafe archive source path")
        require(candidate["format"] in {"file", "tar", "zip"}, "unsupported archive format")
        require(digest(candidate["sha256"]), "invalid source digest")
        if candidate["format"] == "file":
            require(
                candidate["member"] == "" and candidate["container_sha256"] == "",
                "file carries member identity",
            )
        else:
            require(safe_path(candidate["member"]), "unsafe archive member")
            require(digest(candidate["container_sha256"]), "missing container identity")
        identity = (candidate["root"], candidate["path"], candidate["member"])
        require(identity not in identities, "duplicate source identity")
        identities.add(identity)
        require(
            candidate["evidence"] in {"source-inspected", "package-pinned"},
            "archive inspection claims execution",
        )
        require(
            candidate["use"]
            in {
                "mechanism-comparison",
                "negative-control",
                "dependency-audit",
                "authority-reference",
                "package-inspection",
            },
            "unreviewed archive promotion",
        )
        for field in ("license_observation", "family", "invariant", "conflict", "experiment"):
            require(
                isinstance(candidate[field], str) and candidate[field].strip(),
                f"missing candidate {field}",
            )
    require(covered == set(roots), "archive root lacks selected investigation")


def file_digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def source_digest(candidate, corpus):
    source = corpus / candidate["root"] / candidate["path"]
    require(source.resolve().is_relative_to(corpus.resolve()), "source escapes selected corpus")
    require(source.is_file(), "source is not a regular file")
    actual = file_digest(source)
    if candidate["format"] == "file":
        return actual
    require(actual == candidate["container_sha256"], "container digest drift")
    if candidate["format"] == "zip":
        with zipfile.ZipFile(source) as archive:
            matches = [info for info in archive.infolist() if info.filename == candidate["member"]]
            require(len(matches) == 1, "ambiguous or absent ZIP member")
            require(
                not matches[0].is_dir() and matches[0].file_size <= MEMBER_LIMIT,
                "ZIP source exceeds member boundary",
            )
            payload = archive.read(matches[0])
    else:
        with tarfile.open(source, "r:*") as archive:
            matches = [info for info in archive.getmembers() if info.name == candidate["member"]]
            require(len(matches) == 1, "ambiguous or absent tar member")
            require(
                matches[0].isfile() and matches[0].size <= MEMBER_LIMIT,
                "tar source exceeds member boundary",
            )
            with archive.extractfile(matches[0]) as stream:
                payload = stream.read()
    return hashlib.sha256(payload).hexdigest()


def verify_sources(document, corpus):
    for candidate in document["candidates"]:
        require(
            source_digest(candidate, corpus) == candidate["sha256"],
            f"source digest drift: {candidate['id']}",
        )


def verify_pathsets(document, corpus):
    for name, record in document["roots"].items():
        result = subprocess.run(
            ["rg", "--files", "--hidden", "--no-ignore", "-g", "!.git", "-0", "."],
            cwd=corpus / name,
            check=False,
            capture_output=True,
        )
        require(result.returncode in {0, 1}, f"path inventory fails: {name}")
        paths = sorted(path for path in result.stdout.split(b"\0") if path)
        require(
            hashlib.sha256(b"\0".join(paths) + b"\0").hexdigest() == record["pathset_sha256"],
            f"path inventory drift: {name}",
        )
        counts = dict.fromkeys(("regular_files", "symlinks", "other", "bytes"), 0)
        for path in paths:
            observation = (corpus / name / path.decode()).lstat()
            if stat.S_ISREG(observation.st_mode):
                counts["regular_files"] += 1
                counts["bytes"] += observation.st_size
            elif stat.S_ISLNK(observation.st_mode):
                counts["symlinks"] += 1
            else:
                counts["other"] += 1
        require(
            all(value == record[field] for field, value in counts.items()),
            f"inventory counts or file sizes drift: {name}",
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--verify-pathsets", action="store_true")
    arguments = parser.parse_args()
    document = json.loads((arguments.root / FRONTIER).read_text())
    validate(document)
    if arguments.verify_pathsets:
        require(arguments.corpus is not None, "pathset verification requires --corpus")
        verify_pathsets(document, arguments.corpus)
    if arguments.corpus is not None:
        verify_sources(document, arguments.corpus)
    print(
        f"Archive donor frontier: {len(document['roots'])} roots, "
        f"{len(document['candidates'])} selected investigations; target acceptance remains open"
    )


if __name__ == "__main__":
    main()
