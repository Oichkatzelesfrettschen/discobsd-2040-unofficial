"""Validate and optionally derive the bounded SUS requirement entity corpus."""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import io
import json
import tarfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

CORPUS_COLUMNS = (
    "kind",
    "name",
    "issue7_source",
    "issue8_source",
    "edition_relation",
)
KIND_ORDER = {"base-definition": 0, "system-interface": 1, "utility": 2}
EDITION_RELATIONS = {"issue7-only", "issue8-only", "retained"}
SPECIAL_BUILTINS = (
    "break",
    "colon",
    "continue",
    "dot",
    "eval",
    "exec",
    "exit",
    "export",
    "readonly",
    "return",
    "set",
    "shift",
    "times",
    "trap",
    "unset",
)
STRUCTURAL_PAGES = {
    "contents.html",
    "toc.html",
    "V1_title.html",
    "V2_title.html",
    "V3_title.html",
}


class CorpusError(RuntimeError):
    """Report a source, schema, or cross-ledger contradiction."""


@dataclass(frozen=True)
class Entity:
    kind: str
    name: str
    issue7_source: str
    issue8_source: str

    @property
    def edition_relation(self) -> str:
        if self.issue7_source and self.issue8_source:
            return "retained"
        return "issue7-only" if self.issue7_source else "issue8-only"

    def row(self) -> tuple[str, ...]:
        return (
            self.kind,
            self.name,
            self.issue7_source,
            self.issue8_source,
            self.edition_relation,
        )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CorpusError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def ordered_sha256(rows: list[tuple[str, ...]]) -> str:
    encoded = "".join("\t".join(row) + "\n" for row in rows).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def safe_member_parts(name: str) -> tuple[str, ...]:
    path = PurePosixPath(name)
    require(not path.is_absolute(), f"absolute archive member: {name}")
    require(".." not in path.parts, f"parent traversal archive member: {name}")
    return path.parts


def entity_from_page(volume: str, filename: str) -> tuple[str, str] | None:
    if filename in STRUCTURAL_PAGES and not (volume == "functions" and filename == "toc.html"):
        return None
    if volume == "basedefs":
        if filename.startswith("V1_chap") and filename.endswith(".html"):
            return "base-definition", filename.removesuffix(".html")
        if filename.endswith(".h.html"):
            return "base-definition", filename.removesuffix(".html")
        return None
    if volume == "functions":
        if filename == "V2_chap01.html" or not filename.endswith(".html"):
            return None
        return "system-interface", filename.removesuffix(".html")
    if volume == "utilities":
        if (
            filename.startswith("V3_chap")
            or filename == "builtins-redirector.html"
            or not filename.endswith(".html")
        ):
            return None
        return "utility", filename.removesuffix(".html")
    return None


def require_issue7_special_source(source_paths: set[str]) -> str:
    special_source = "utilities/V3_chap02.html"
    require(
        special_source in source_paths,
        "Issue 7 special-builtin source chapter is absent",
    )
    return special_source


def archive_pages(path: Path, edition: str) -> dict[tuple[str, str], str]:
    pages: dict[tuple[str, str], str] = {}
    if edition == "issue7":
        with tarfile.open(path, "r:gz") as archive:
            member_names = [member.name for member in archive.getmembers() if member.isfile()]
    else:
        with zipfile.ZipFile(path) as archive:
            member_names = [item.filename for item in archive.infolist() if not item.is_dir()]

    source_paths: set[str] = set()
    for member_name in member_names:
        parts = safe_member_parts(member_name)
        if len(parts) != 3:
            continue
        _top_directory, volume, filename = parts
        source_paths.add(f"{volume}/{filename}")
        identity = entity_from_page(volume, filename)
        if identity is None:
            continue
        require(identity not in pages, f"duplicate {edition} entity: {identity}")
        pages[identity] = f"{volume}/{filename}"

    if edition == "issue7":
        special_source = require_issue7_special_source(source_paths)
        for builtin_name in SPECIAL_BUILTINS:
            identity = ("utility", builtin_name)
            require(identity not in pages, f"duplicate Issue 7 special builtin: {builtin_name}")
            pages[identity] = special_source
    return pages


def derive_entities(issue7_archive: Path, issue8_archive: Path) -> list[Entity]:
    issue7_pages = archive_pages(issue7_archive, "issue7")
    issue8_pages = archive_pages(issue8_archive, "issue8")
    identities = sorted(
        set(issue7_pages) | set(issue8_pages),
        key=lambda identity: (KIND_ORDER[identity[0]], identity[1]),
    )
    return [
        Entity(
            kind=kind,
            name=name,
            issue7_source=issue7_pages.get((kind, name), ""),
            issue8_source=issue8_pages.get((kind, name), ""),
        )
        for kind, name in identities
    ]


def render_entities(entities: list[Entity]) -> str:
    output = io.StringIO()
    writer = csv.writer(output, delimiter="\t", lineterminator="\n")
    writer.writerow(CORPUS_COLUMNS)
    writer.writerows(entity.row() for entity in entities)
    return output.getvalue()


def parse_entities(path: Path) -> list[Entity]:
    try:
        text = path.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as error:
        raise CorpusError(f"cannot read ASCII entity corpus {path}: {error}") from error
    reader = csv.DictReader(io.StringIO(text), delimiter="\t")
    require(tuple(reader.fieldnames or ()) == CORPUS_COLUMNS, "entity corpus columns drift")
    entities: list[Entity] = []
    for line_number, row in enumerate(reader, 2):
        require(None not in row and None not in row.values(), f"malformed entity row {line_number}")
        require(row["kind"] in KIND_ORDER, f"unknown entity kind on row {line_number}")
        require(bool(row["name"]), f"empty entity name on row {line_number}")
        entity = Entity(
            kind=row["kind"],
            name=row["name"],
            issue7_source=row["issue7_source"],
            issue8_source=row["issue8_source"],
        )
        require(
            row["edition_relation"] in EDITION_RELATIONS,
            f"unknown edition relation on row {line_number}",
        )
        require(
            row["edition_relation"] == entity.edition_relation,
            f"edition relation contradicts sources on row {line_number}",
        )
        entities.append(entity)
    return entities


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CorpusError(f"cannot read corpus metadata {path}: {error}") from error


def utility_names(path: Path) -> set[str]:
    document = load_json(path)
    names: set[str] = set()
    for group in document.get("groups", []):
        for name in group.get("names", []):
            require(name not in names, f"duplicate existing utility row: {name}")
            names.add(name)
    return names


def validate_document(
    root: Path,
    metadata: dict,
    entities: list[Entity],
    utility_ledger: Path,
) -> dict[str, int]:
    require(metadata.get("schema_version") == 1, "unsupported corpus schema")
    authorities = metadata.get("authorities")
    require(isinstance(authorities, dict), "corpus lacks authorities")
    for edition in ("issue7", "issue8"):
        authority = authorities.get(edition)
        require(isinstance(authority, dict), f"corpus lacks {edition} authority")
        require(len(authority.get("archive_sha256", "")) == 64, f"bad {edition} archive hash")
        require(bool(authority.get("standard")), f"corpus lacks {edition} standard identity")
        require(bool(authority.get("public_base")), f"corpus lacks {edition} public base")

    rows = [entity.row() for entity in entities]
    expected_order = sorted(rows, key=lambda row: (KIND_ORDER[row[0]], row[1]))
    require(rows == expected_order, "entity corpus is not in canonical order")
    identities = [(entity.kind, entity.name) for entity in entities]
    require(len(identities) == len(set(identities)), "entity corpus contains duplicate identities")

    counts: dict[str, int] = {kind: 0 for kind in KIND_ORDER}
    edition_counts = {"issue7": 0, "issue8": 0, "retained": 0}
    for entity in entities:
        counts[entity.kind] += 1
        if entity.issue7_source:
            edition_counts["issue7"] += 1
        if entity.issue8_source:
            edition_counts["issue8"] += 1
        if entity.edition_relation == "retained":
            edition_counts["retained"] += 1

    corpus = metadata.get("corpus")
    require(isinstance(corpus, dict), "corpus metadata is absent")
    require(corpus.get("entity_count") == len(entities), "entity count drift")
    require(corpus.get("kind_counts") == counts, "entity kind counts drift")
    require(corpus.get("edition_counts") == edition_counts, "edition counts drift")
    require(corpus.get("ordered_row_sha256") == ordered_sha256(rows), "entity row hash drift")

    source_ledgers = metadata.get("issue7_source_ledgers")
    require(isinstance(source_ledgers, dict), "Issue 7 source-ledger metadata is absent")
    base_ledger = source_ledgers.get("base-definitions", {})
    system_ledger = source_ledgers.get("system-interfaces", {})
    shell_ledger = source_ledgers.get("shell-and-utilities", {})
    require(base_ledger.get("parent_count") == 95, "Issue 7 Base parent count drift")
    require(base_ledger.get("clause_count") == 1483, "Issue 7 Base clause count drift")
    require(system_ledger.get("parent_count") == 1195, "Issue 7 interface parent count drift")
    require(system_ledger.get("clause_count") == 15740, "Issue 7 interface clause count drift")
    require(shell_ledger.get("utility_count") == 175, "Issue 7 utility count drift")
    require(shell_ledger.get("token_requirement_count") == 17, "shell token count drift")
    require(shell_ledger.get("grammar_requirement_count") == 125, "shell grammar count drift")
    require(
        shell_ledger.get("recursive_frontier_requirement_count") == 2644,
        "recursive frontier count drift",
    )
    for ledger in (base_ledger, system_ledger, shell_ledger):
        for field, value in ledger.items():
            if field.endswith("sha256"):
                require(isinstance(value, str) and len(value) == 64, f"bad ledger hash: {field}")
    issue7_kind_counts = {kind: 0 for kind in KIND_ORDER}
    for entity in entities:
        if entity.issue7_source:
            issue7_kind_counts[entity.kind] += 1
    require(
        issue7_kind_counts["base-definition"] == base_ledger["parent_count"],
        "Issue 7 Base entities differ from the source ledger",
    )
    require(
        issue7_kind_counts["system-interface"] == system_ledger["parent_count"],
        "Issue 7 interface entities differ from the source ledger",
    )
    require(
        issue7_kind_counts["utility"] == shell_ledger["utility_count"],
        "Issue 7 utility entities differ from the source ledger",
    )

    facets = metadata.get("requirement_facets")
    require(isinstance(facets, dict), "requirement facet taxonomy is absent")
    for kind in KIND_ORDER:
        values = facets.get(kind)
        require(isinstance(values, list) and values, f"missing facets for {kind}")
        require(len(values) == len(set(values)), f"duplicate facets for {kind}")

    mappings = metadata.get("tiny_system_mappings")
    require(isinstance(mappings, list) and mappings, "tiny-system mappings are absent")
    mapped_ids: set[str] = set()
    entity_ids = {f"{entity.kind}:{entity.name}" for entity in entities}
    allowed_states = {"bounded", "partial", "open", "profile-unselected"}
    for mapping in mappings:
        mapping_id = mapping.get("id")
        require(isinstance(mapping_id, str) and mapping_id, "mapping lacks id")
        require(mapping_id not in mapped_ids, f"duplicate mapping: {mapping_id}")
        mapped_ids.add(mapping_id)
        require(mapping.get("state") in allowed_states, f"bad mapping state: {mapping_id}")
        mapped_entities = mapping.get("entities")
        require(
            isinstance(mapped_entities, list) and mapped_entities,
            f"mapping lacks entities: {mapping_id}",
        )
        for entity_id in mapped_entities:
            require(entity_id in entity_ids, f"mapping names absent entity: {entity_id}")
        behaviors = mapping.get("behaviors")
        require(isinstance(behaviors, list) and behaviors, f"mapping lacks behaviors: {mapping_id}")
        require(
            bool(mapping.get("tiny_translation")), f"mapping lacks tiny translation: {mapping_id}"
        )
        require(bool(mapping.get("residual")), f"mapping lacks residual: {mapping_id}")
        evidence_paths = mapping.get("evidence")
        require(
            isinstance(evidence_paths, list) and evidence_paths,
            f"mapping lacks evidence: {mapping_id}",
        )
        for evidence in evidence_paths:
            require(
                isinstance(evidence, str) and evidence,
                f"mapping has invalid evidence path: {mapping_id}",
            )
            evidence_path = PurePosixPath(evidence)
            require(
                not evidence_path.is_absolute() and ".." not in evidence_path.parts,
                f"mapping evidence escapes repository: {evidence}",
            )
            resolved_evidence = (root / evidence).resolve()
            require(
                resolved_evidence.is_relative_to(root) and resolved_evidence.is_file(),
                f"mapping evidence is not a repository file: {evidence}",
            )

    standard_utility_names = {entity.name for entity in entities if entity.kind == "utility"}
    owned_names = utility_names(utility_ledger)
    missing_owned_names = sorted(owned_names - standard_utility_names)
    declared_extensions = metadata.get("selected_image_extensions")
    require(
        isinstance(declared_extensions, list),
        "selected-image extension list is absent",
    )
    require(
        len(declared_extensions) == len(set(declared_extensions)),
        "selected-image extension list contains duplicates",
    )
    require(
        missing_owned_names == sorted(declared_extensions),
        "selected-image nonstandard utility set drift: " + ", ".join(missing_owned_names),
    )
    require(
        not (set(declared_extensions) & standard_utility_names),
        "selected-image extension is present in the standards corpus",
    )
    expected_owned_count = metadata.get("selected_image_utility_count")
    require(expected_owned_count == len(owned_names), "selected-image utility count drift")
    return counts


def expect_bad(label: str, action) -> None:
    try:
        action()
    except CorpusError:
        return
    raise CorpusError(f"negative control passed: {label}")


def self_test(root: Path, metadata: dict, entities: list[Entity], utility_ledger: Path) -> None:
    validate_document(root, metadata, entities, utility_ledger)

    expect_bad(
        "absent Issue 7 special-builtin source",
        lambda: require_issue7_special_source(set()),
    )

    bad_count = copy.deepcopy(metadata)
    bad_count["corpus"]["entity_count"] += 1
    expect_bad("entity count", lambda: validate_document(root, bad_count, entities, utility_ledger))

    bad_hash = copy.deepcopy(metadata)
    bad_hash["corpus"]["ordered_row_sha256"] = "0" * 64
    expect_bad("row hash", lambda: validate_document(root, bad_hash, entities, utility_ledger))

    duplicate = entities + [entities[-1]]
    expect_bad(
        "duplicate entity", lambda: validate_document(root, metadata, duplicate, utility_ledger)
    )

    bad_mapping = copy.deepcopy(metadata)
    bad_mapping["tiny_system_mappings"][0]["entities"] = ["system-interface:absent_contract"]
    expect_bad(
        "absent mapped entity",
        lambda: validate_document(root, bad_mapping, entities, utility_ledger),
    )

    for label, evidence in (
        ("empty evidence", []),
        ("absolute evidence", ["/etc/passwd"]),
        ("parent-traversing evidence", ["../outside"]),
        ("directory evidence", ["docs"]),
    ):
        bad_evidence = copy.deepcopy(metadata)
        bad_evidence["tiny_system_mappings"][0]["evidence"] = evidence
        expect_bad(
            label,
            lambda document=bad_evidence: validate_document(
                root, document, entities, utility_ledger
            ),
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--metadata",
        type=Path,
        default=Path("docs/research/sus-requirement-corpus.json"),
    )
    parser.add_argument(
        "--entities",
        type=Path,
        default=Path("docs/research/sus-requirement-entities.tsv"),
    )
    parser.add_argument(
        "--utility-ledger",
        type=Path,
        default=Path("docs/research/sus-utility-ledger.json"),
    )
    parser.add_argument("--issue7-archive", type=Path)
    parser.add_argument("--issue8-archive", type=Path)
    parser.add_argument("--derive", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def resolve(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def main() -> int:
    arguments = parse_args()
    root = arguments.root.resolve()
    if arguments.derive:
        require(arguments.issue7_archive is not None, "--derive requires --issue7-archive")
        require(arguments.issue8_archive is not None, "--derive requires --issue8-archive")
        entities = derive_entities(arguments.issue7_archive, arguments.issue8_archive)
        print(render_entities(entities), end="")
        return 0

    metadata_path = resolve(root, arguments.metadata)
    entities_path = resolve(root, arguments.entities)
    utility_ledger = resolve(root, arguments.utility_ledger)
    metadata = load_json(metadata_path)
    entities = parse_entities(entities_path)
    counts = validate_document(root, metadata, entities, utility_ledger)

    if arguments.issue7_archive is not None or arguments.issue8_archive is not None:
        require(
            arguments.issue7_archive is not None, "source verification requires Issue 7 archive"
        )
        require(
            arguments.issue8_archive is not None, "source verification requires Issue 8 archive"
        )
        authorities = metadata["authorities"]
        require(
            sha256_file(arguments.issue7_archive) == authorities["issue7"]["archive_sha256"],
            "Issue 7 archive hash mismatch",
        )
        require(
            sha256_file(arguments.issue8_archive) == authorities["issue8"]["archive_sha256"],
            "Issue 8 archive hash mismatch",
        )
        derived = derive_entities(arguments.issue7_archive, arguments.issue8_archive)
        require(derived == entities, "committed entity corpus differs from source archives")

    if arguments.self_test:
        self_test(root, metadata, entities, utility_ledger)
    print(
        "SUS requirement corpus: "
        f"{len(entities)} entities, {counts['base-definition']} base definitions, "
        f"{counts['system-interface']} interfaces, {counts['utility']} utilities"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CorpusError as error:
        raise SystemExit(f"SUS requirement corpus: {error}") from error
