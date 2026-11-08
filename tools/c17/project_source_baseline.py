"""Project a C17 shipped closure into the source-baseline companion.

The companion (docs/research/c17-shipped-source-baseline.json) restates the
closure that tools/c17/shipped_closure.py writes: `sources` maps each tracked
path in any group's `tracked_sources` to its SHA-256, `artifact_sources` lists
each group's sorted `tracked_sources`, `closure_sha256` is the closure file's
digest (the value the resource receipt binds as `execution.closure_sha256`),
and `source_commit` and `tracked_files_sha256` come from the closure
provenance. `schema_version` and `language_acceptance` carry over from the
existing companion.

The source hashes come from the retained source snapshot the capture took
(`.build-logs/c17-source.json`), never from the working tree: the snapshot's
digest must equal the closure's `provenance.source_snapshot_sha256`, so every
hash in the companion is the one the measured build compiled.

usage: project_source_baseline.py --closure FILE --snapshot FILE
       --template FILE --output FILE
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path


class ProjectionError(Exception):
    """The closure and the snapshot do not describe one capture."""


def project(closure_path: Path, snapshot_path: Path,
            template_path: Path) -> dict[str, object]:
    raw = closure_path.read_bytes()
    closure = json.loads(raw)
    snapshot_raw = snapshot_path.read_bytes()
    provenance = closure["provenance"]
    if hashlib.sha256(snapshot_raw).hexdigest() != provenance["source_snapshot_sha256"]:
        raise ProjectionError(f"{snapshot_path} is not the snapshot {closure_path} binds")
    recorded = json.loads(snapshot_raw)["tracked_files"]
    template = json.loads(template_path.read_text(encoding="utf-8"))
    artifacts: dict[str, list[str]] = {}
    sources: dict[str, str] = {}
    for name, group in closure["configurations"].items():
        tracked = sorted(group["tracked_sources"])
        artifacts[name] = tracked
        for path in tracked:
            if path not in recorded:
                raise ProjectionError(f"{path} is absent from the source snapshot")
            sources[path] = recorded[path]
    result = dict(template)
    result["artifact_sources"] = dict(sorted(artifacts.items()))
    result["sources"] = dict(sorted(sources.items()))
    result["closure_sha256"] = hashlib.sha256(raw).hexdigest()
    result["source_commit"] = provenance["source_commit"]
    result["tracked_files_sha256"] = provenance["tracked_files_sha256"]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Project a C17 shipped closure.")
    parser.add_argument("--closure", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = project(args.closure, args.snapshot, args.template)
    except ProjectionError as error:
        print(f"FAIL source projection: {error}", file=sys.stderr)
        return 1
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
