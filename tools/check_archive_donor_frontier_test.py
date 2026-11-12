"""Calibrate archive-frontier accounting and file/container/member identities."""

import copy
import hashlib
import io
import json
import tarfile
import tempfile
import warnings
import zipfile
from pathlib import Path

import check_archive_donor_frontier as frontier


def reject(label, function, *arguments):
    try:
        function(*arguments)
    except frontier.FrontierError:
        return
    raise AssertionError(f"archive frontier accepts {label}")


def main():
    root = Path(__file__).resolve().parents[1]
    document = json.loads((root / frontier.FRONTIER).read_text())
    frontier.validate(document)
    mutations = [
        ("schema", ["schema_version"], True),
        ("claim", ["claim"], document["claim"] + " All requirements pass."),
        ("denominator", ["candidate_order"], []),
        ("empty sources", ["candidates"], []),
        ("root identity", ["candidates", 0, "root"], "missing"),
        ("source escape", ["candidates", 0, "path"], "../escape.c"),
        ("source digest", ["candidates", 0, "sha256"], "0" * 64),
        ("execution promotion", ["candidates", 0, "evidence"], "board"),
        ("import promotion", ["candidates", 0, "use"], "approved-import"),
        ("missing experiment", ["candidates", 0, "experiment"], ""),
        ("missing license observation", ["candidates", 0, "license_observation"], ""),
        ("member escape", ["candidates", -1, "member"], "../escape.c"),
        ("missing container identity", ["candidates", -1, "container_sha256"], ""),
        ("unsupported container", ["candidates", -1, "format"], "execute"),
        ("invalid count", ["roots", frontier.ROOTS[0], "regular_files"], -1),
        ("invalid pathset", ["roots", frontier.ROOTS[0], "pathset_sha256"], "0" * 64),
        ("closed root", ["roots", frontier.ROOTS[0], "next_experiment"], ""),
    ]
    for label, keys, value in mutations:
        changed = copy.deepcopy(document)
        target = changed
        for key in keys[:-1]:
            target = target[key]
        target[keys[-1]] = value
        reject(label, frontier.validate, changed)
    changed = copy.deepcopy(document)
    del changed["roots"][frontier.ROOTS[-1]]
    reject("missing root", frontier.validate, changed)
    with tempfile.TemporaryDirectory(prefix="archive-frontier-") as directory:
        corpus = Path(directory)
        (corpus / "fixture").mkdir()
        payload = b"typed source fixture\n"
        source_hash = hashlib.sha256(payload).hexdigest()
        candidate = {
            "root": "fixture",
            "path": "source.c",
            "member": "",
            "container_sha256": "",
            "format": "file",
            "sha256": source_hash,
        }
        (corpus / "fixture/source.c").write_bytes(payload)
        assert frontier.source_digest(candidate, corpus) == source_hash
        for archive_format in ("zip", "tar"):
            path = corpus / "fixture" / ("source." + archive_format)
            if archive_format == "zip":
                with zipfile.ZipFile(path, "w") as archive:
                    archive.writestr("source.c", payload)
            else:
                with tarfile.open(path, "w") as archive:
                    info = tarfile.TarInfo("source.c")
                    info.size = len(payload)
                    archive.addfile(info, io.BytesIO(payload))
            member = {
                **candidate,
                "path": path.name,
                "format": archive_format,
                "member": "source.c",
                "container_sha256": frontier.file_digest(path),
            }
            assert frontier.source_digest(member, corpus) == source_hash
            changed = {**member, "container_sha256": "a" * 64}
            reject("container substitution", frontier.source_digest, changed, corpus)
            changed = {**member, "member": "absent.c"}
            reject("absent member", frontier.source_digest, changed, corpus)
            changed = {**member, "id": "fixture", "sha256": "a" * 64}
            reject(
                "member substitution", frontier.verify_sources, {"candidates": [changed]}, corpus
            )
        pathset = hashlib.sha256(b"./source.c\0./source.tar\0./source.zip\0").hexdigest()
        record = {
            "pathset_sha256": pathset,
            "regular_files": 3,
            "symlinks": 0,
            "other": 0,
            "bytes": sum(path.stat().st_size for path in (corpus / "fixture").iterdir()),
        }
        frontier.verify_pathsets({"roots": {"fixture": record}}, corpus)
        changed = {**record, "bytes": record["bytes"] + 1}
        reject(
            "inventory size drift",
            frontier.verify_pathsets,
            {"roots": {"fixture": changed}},
            corpus,
        )
        (corpus / "fixture/source.c").rename(corpus / "fixture/renamed.c")
        reject(
            "inventory path drift", frontier.verify_pathsets, {"roots": {"fixture": record}}, corpus
        )
        for archive_format in ("zip", "tar"):
            archive_path = corpus / "fixture" / ("boundary." + archive_format)

            def write_members(
                member_payloads,
                symlink=False,
                archive_format=archive_format,
                archive_path=archive_path,
            ):
                if archive_format == "zip":
                    with warnings.catch_warnings(record=True) as captured:
                        warnings.simplefilter("always", UserWarning)
                        with zipfile.ZipFile(archive_path, "w") as archive:
                            for member_payload in member_payloads:
                                archive.writestr("source.c", member_payload)
                        assert len(captured) == max(0, len(member_payloads) - 1)
                        assert all("Duplicate name" in str(item.message) for item in captured)
                else:
                    with tarfile.open(archive_path, "w") as archive:
                        for member_payload in member_payloads:
                            info = tarfile.TarInfo("source.c")
                            if symlink:
                                info.type = tarfile.SYMTYPE
                                info.linkname = "../outside.c"
                                archive.addfile(info)
                            else:
                                info.size = len(member_payload)
                                archive.addfile(info, io.BytesIO(member_payload))
                return {
                    **candidate,
                    "path": archive_path.name,
                    "format": archive_format,
                    "member": "source.c",
                    "container_sha256": frontier.file_digest(archive_path),
                }

            boundary_payload = b"x" * frontier.MEMBER_LIMIT
            member = write_members([boundary_payload])
            assert (
                frontier.source_digest(member, corpus)
                == hashlib.sha256(boundary_payload).hexdigest()
            )
            member = write_members([boundary_payload + b"x"])
            reject("oversized member", frontier.source_digest, member, corpus)
            member = write_members([payload, payload])
            reject("duplicate member", frontier.source_digest, member, corpus)
            if archive_format == "tar":
                member = write_members([b""], symlink=True)
                reject("tar symlink member", frontier.source_digest, member, corpus)
        with tempfile.TemporaryDirectory(prefix="archive-outside-") as outside:
            external_source = Path(outside) / "source.c"
            external_source.write_bytes(payload)
            (corpus / "fixture/external.c").symlink_to(external_source)
            reject(
                "source symlink escape",
                frontier.source_digest,
                {**candidate, "path": "external.c"},
                corpus,
            )
    print("PASS archive frontier: 18 schema mutations and 14 identity/inventory failures reject")


if __name__ == "__main__":
    main()
