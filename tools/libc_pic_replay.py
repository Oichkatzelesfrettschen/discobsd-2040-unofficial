"""Replay captured ARM libc members with explicit process-local PIC flags."""

import argparse
import collections
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from libc_shared_closure import command, read_archive, read_member

sys.path.insert(0, str(Path(__file__).resolve().parent / "c17"))
from record_tool import digest  # noqa: E402
from shipped_closure import verify_snapshot  # noqa: E402

PIC_FLAGS = ("-DDISCOBSD_SHARED_LIBC=1", "-fPIC", "-msingle-pic-base",
             "-mpic-register=r9", "-mno-pic-data-is-text-relative")
OPTIONS = frozenset({
    "-", "-o", "-c", "-x", "-Os", "-Wall", "-Werror", "-Wextra", "-Wformat",
    "-Wformat-security", "-Wundef", "-ffreestanding", "-ffunction-sections",
    "-fno-common", "-fno-delete-null-pointer-checks", "-mabi=aapcs",
    "-mcpu=cortex-m0plus", "-mfloat-abi=soft", "-mlittle-endian", "-mthumb",
    "-nostdinc", "-std=gnu17", "-DCOMPACT_INODE_FIELDS", "-DCOMPACT_SWAPMAP",
    "-DLINEAR_INODE_CACHE", "-DNMOUNT=1", "-DSINGLE_UFS_ROOT", "-D_TIME_BITS=32",
    "-D_TIME_BITS=64", "-U_TIME_BITS",
})


def require(condition, message):
    if not condition:
        raise ValueError(message)


def contained(base, spelling):
    path = Path(spelling)
    require(not path.is_absolute() and path.as_posix() == spelling and
            ".." not in path.parts, "nonrelative capture path: " + spelling)
    resolved = (base / path).resolve()
    require(resolved.is_relative_to(base.resolve()), "capture path escapes its owner")
    return resolved


def check_operands(root, directory, row):
    sources = {contained(root, item["path"]) for item in row["sources"]}
    outputs = {contained(root, item["path"]) for item in row["outputs"]}
    require(len(outputs) == 1, "compilation must own one output")
    observed = set()
    arguments = iter(row["argv"][1:])
    for argument in arguments:
        if argument == "-o":
            operand = next(arguments, None)
            require(operand is not None and (directory / operand).resolve() in outputs,
                    "output operand differs from capture")
        elif argument == "-x":
            require(next(arguments, None) == "assembler-with-cpp",
                    "unreviewed compiler input language")
        elif argument.startswith("@"):
            raise ValueError("unreviewed compiler operand: response input")
        elif not argument.startswith("-"):
            source = (directory / argument).resolve()
            require(source in sources, "source operand differs from capture")
            observed.add(source)
    require(observed == sources, "source operand witnesses differ")


def prepare(root, records, snapshot, archive, compiler, ar):
    """Bind every selected static member to one unambiguous compilation witness."""
    verify_snapshot(root, snapshot)
    snapshot_hash = digest(snapshot)
    compiler_hash = digest(compiler)
    graph = read_archive(archive, ar)
    candidates = collections.defaultdict(list)
    for path in sorted(records.glob("*.json")):
        content = path.read_bytes()
        row = json.loads(content)
        require(isinstance(row, dict), "capture must be an object")
        if row.get("kind") != "compile":
            continue
        require(row.get("schema_version") == 1, "unsupported compilation capture version")
        for output in row["outputs"]:
            candidates[(Path(output["path"]).name, output["sha256"])].append(
                (path, row, hashlib.sha256(content).hexdigest()))
    selected = []
    for name, member in graph["members"].items():
        matches = candidates[(name, member["sha256"])]
        require(matches, "missing compilation witness: " + name)
        identities = {json.dumps({key: row[key] for key in
                                 ("directory", "argv", "sources", "dependencies", "tool",
                                  "source_snapshot_sha256", "exit_code", "capture_error")},
                                sort_keys=True) + str(row.get("stdin_sha256"))
                      for _, row, _ in matches}
        require(len(identities) == 1, "ambiguous compilation witnesses: " + name)
        path, row, record_hash = matches[-1]
        require(row["exit_code"] == 0 and row["capture_error"] is None,
                "unsuccessful compilation capture: " + name)
        require(row["source_snapshot_sha256"] == snapshot_hash, "snapshot receipt differs")
        argv = row["argv"][:]
        require(Path(argv[0]).resolve() == compiler.resolve() and
                row["tool"]["sha256"] == compiler_hash, "compiler identity differs")
        directory = contained(root, row["directory"])
        require(directory.is_relative_to(root / "lib/libc"), "compile owner is outside libc")
        require(row["dependencies"], "compilation has no dependency witnesses")
        for item in row["sources"] + row["dependencies"]:
            require(digest(contained(root, item["path"])) == item["sha256"],
                    "compilation input differs: " + item["path"])
        require(argv.count("-o") <= 1 and "-c" in argv,
                "unsupported output or compilation mode: " + name)
        check_operands(root, directory, row)
        for argument in argv[1:]:
            if argument.startswith("-"):
                require(argument in OPTIONS or argument in
                        {"-I" + str(root / "include"), "-fdebug-prefix-map=" + str(root) + "=."},
                        "unreviewed compiler option: " + argument)
        if "-o" not in argv:
            require(len(row["sources"]) == 1, "implicit output lacks a single source")
            argv.extend(["-o", name])
        require(argv.index("-o") + 1 < len(argv), "output operand is missing")
        stdin = None
        if row.get("stdin_retained"):
            stdin = contained(records, row["stdin_retained"]).read_bytes()
            require(hashlib.sha256(stdin).hexdigest() == row["stdin_sha256"],
                    "retained assembly input differs")
        require(argv.count("-") == int(stdin is not None), "standard input witness differs")
        selected.append({"name": name, "row": row, "record": path,
                         "record_sha256": record_hash,
                         "argv": argv, "stdin": stdin, "directory": directory,
                         "static_sha256": member["sha256"]})
    return graph, selected


def replay(root, records, snapshot, archive, output, compiler, ar):
    root, records, snapshot, archive, output = [p.resolve() for p in
                                               (root, records, snapshot, archive, output)]
    require(output.is_relative_to(root / ".build-logs") and
            output != root / ".build-logs", "output must lie below the evidence directory")
    require(not output.exists(), "replay output already exists")
    archive_hash = digest(archive)
    snapshot_hash, compiler_hash, ar_hash = digest(snapshot), digest(compiler), digest(Path(ar))
    graph, selected = prepare(root, records, snapshot, archive, compiler, ar)
    verifier_hash = digest(Path(__file__))
    output.mkdir(parents=True)
    results = []
    for item in selected:
        target = output / item["name"]
        argv = item["argv"][:]
        argv[argv.index("-o") + 1] = str(target)
        argv.extend(PIC_FLAGS)
        result = subprocess.run(argv, cwd=item["directory"], input=item["stdin"],
                                capture_output=True, check=False, timeout=60,
                                env=dict(os.environ, LC_ALL="C"))
        (output / (item["name"] + ".log")).write_bytes(result.stdout + result.stderr)
        require(result.returncode == 0 and not result.stdout and not result.stderr,
                "PIC compilation fails or emits diagnostics: " + item["name"])
        read_member(item["name"], target.read_bytes())
        results.append({"member": item["name"], "static_sha256": item["static_sha256"],
                        "pic_sha256": digest(target), "capture_sha256": item["record_sha256"],
                        "directory": item["row"]["directory"], "exit_code": 0,
                        "diagnostic_bytes": 0})
    candidate = output / "libc-pic.a"
    command([ar, "cr", candidate, *(output / item["name"] for item in selected)])
    pic_graph = read_archive(candidate, ar)
    require(set(pic_graph["members"]) == set(graph["members"]), "PIC member denominator differs")
    require(all(pic_graph["members"][row["member"]]["sha256"] == row["pic_sha256"]
                for row in results), "PIC object changes before archiving")
    require(digest(archive) == archive_hash, "static archive changes during replay")
    require(digest(snapshot) == snapshot_hash, "snapshot receipt changes during replay")
    require(all(digest(item["record"]) == item["record_sha256"] for item in selected),
            "compilation receipt changes during replay")
    # Every captured source and dependency, tracked or not: verify_snapshot
    # covers only tracked files, so an untracked input that changes while
    # the members compile would otherwise reach the receipt.
    require(all(digest(contained(root, entry["path"])) == entry["sha256"]
                for item in selected
                for entry in item["row"]["sources"] + item["row"]["dependencies"]),
            "compilation input changes during replay")
    require(all(hashlib.sha256(contained(records, item["row"]["stdin_retained"]).read_bytes())
                .hexdigest() == item["row"]["stdin_sha256"]
                for item in selected if item["stdin"] is not None),
            "retained assembly input changes during replay")
    require(digest(compiler) == compiler_hash and digest(Path(ar)) == ar_hash,
            "compiler or archiver changes during replay")
    verify_snapshot(root, snapshot)
    require(digest(Path(__file__)) == verifier_hash, "replay verifier changes during execution")
    receipt = {"schema_version": 1, "source_commit": json.loads(snapshot.read_text())["commit"],
               "source_snapshot_sha256": snapshot_hash, "verifier_sha256": verifier_hash,
               "static_archive_sha256": archive_hash, "pic_archive_sha256": digest(candidate),
               "compiler": {"name": compiler.name, "sha256": compiler_hash,
                            "version": command([compiler, "--version"]).decode().splitlines()[0]},
               "archiver": {"name": Path(ar).name, "sha256": ar_hash},
               "pic_flags": list(PIC_FLAGS), "members": results,
               "claim": "Compiler acceptance and member identity; shared linking, API selection, "
                        "state relocation, runtime and physical qualification remain separate."}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="arm-none-eabi-gcc")
    parser.add_argument("--ar", default="arm-none-eabi-ar")
    args = parser.parse_args()
    try:
        require(os.environ.get("PYTHON"), "PYTHON must select the interpreter")
        compiler, ar = shutil.which(args.compiler), shutil.which(args.ar)
        require(compiler and ar, "ARM GCC and binutils are required")
        result = replay(args.root, args.records, args.snapshot, args.archive,
                        args.output, Path(compiler), ar)
        print(f"PASS shared PIC replay: {len(result['members'])} archive members")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("libc-pic-replay: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
