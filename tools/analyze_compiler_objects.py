"""Compare identity-bound RP2040 -O0 and -Os compiler objects."""

import argparse
import hashlib
import io
import json
import os
import resource
import subprocess
import sys
from pathlib import Path

import compiler_matrix_identity as identity
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

OUTPUT_LIMIT = 128 * 1024 * 1024


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"expected a regular file: {path}")
    return path


def limit_output():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (OUTPUT_LIMIT, OUTPUT_LIMIT))


def object_details(path):
    data = path.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    if (elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM" or
            elf.header.e_type != "ET_REL"):
        raise ValueError(f"not a little-endian ARM32 relocatable object: {path.name}")
    sections = [{"name": section.name, "size": section.header.sh_size,
                 "flags": section.header.sh_flags, "type": section.header.sh_type}
                for section in elf.iter_sections()]
    symbol_table = elf.get_section_by_name(".symtab")
    if symbol_table is None or symbol_table.header.sh_type != "SHT_SYMTAB":
        raise ValueError(f"object has no symbol table: {path.name}")
    functions = []
    for symbol in symbol_table.iter_symbols():
        entry = symbol.entry
        if entry.st_info.type == "STT_FUNC" and symbol.name:
            functions.append({"name": symbol.name, "size": entry.st_size,
                              "value": entry.st_value, "section": entry.st_shndx,
                              "binding": entry.st_info.bind})
    return {"sections": sections, "functions": functions,
            "exec_bytes": sum(section["size"] for section in sections
                              if section["flags"] & 6 == 6)}


def unique_functions(details):
    grouped = {}
    for function in details["functions"]:
        grouped.setdefault(function["name"], []).append(function)
    return {name: values[0]["size"] for name, values in grouped.items()
            if len(values) == 1 and values[0]["size"] > 0}


def analyze(args):
    capture = args.capture.resolve(strict=True)
    output = args.output.resolve()
    if output.is_relative_to(capture):
        raise ValueError("output must be outside the input capture")
    inputs = {name: regular(capture / name) for name in
              ("source-manifest.json", "tool-inputs.json", "results.json")}
    hashes = {name: digest(path) for name, path in inputs.items()}
    source = json.loads(inputs["source-manifest.json"].read_text(encoding="utf-8"))
    if source.get("schema") != 1 or not all(key in source for key in
                                             ("head", "index_sha256", "tracked", "generated")):
        raise ValueError("capture source manifest is malformed")
    tool_inputs = json.loads(inputs["tool-inputs.json"].read_text(encoding="utf-8"))
    rows = json.loads(inputs["results.json"].read_text(encoding="utf-8"))
    if not isinstance(rows, list):
        raise ValueError("capture results are not a row list")
    selected = {}
    for level in ("O0", "Os"):
        matches = [row for row in rows
                   if row.get("compiler") == args.compiler and row.get("level") == level]
        if len(matches) != 1:
            raise ValueError(f"expected one {args.compiler} -{level} row")
        selected[level] = matches[0]
    tool_key = "arm13_objdump" if args.compiler == "arm13" else "gcc16_objdump"
    tool = args.objdump.resolve(strict=True)
    tool_hash = digest(regular(tool))
    if tool_inputs.get(tool_key, {}).get("sha256") != tool_hash:
        raise ValueError("objdump differs from the matrix tool identity")
    for level, row in selected.items():
        row_dir = capture / f"{args.compiler}-{level}"
        if row_dir.is_symlink() or not row_dir.is_dir():
            raise ValueError(f"matrix row directory is invalid: {row_dir}")
        identity.verify_artifacts(row_dir, row.get("artifacts_sha256"))
        reports = {name: value for name, value in row["artifacts_sha256"].items()
                   if "/" not in name and name.endswith(".su")}
        if not reports or reports != row.get("stack_usage_sha256"):
            raise ValueError(f"{level} stack-usage reports are missing or differ")
    objects = {level: {path.name for path in (capture / f"{args.compiler}-{level}").glob("*.o")}
               for level in ("O0", "Os")}
    if not objects["O0"] or objects["O0"] != objects["Os"]:
        raise ValueError("matched rows do not contain the same nonempty object set")
    output.mkdir(parents=False, exist_ok=False)
    environment = {"PATH": os.environ.get("PATH", os.defpath), "LC_ALL": "C", "TZ": "UTC"}
    version = subprocess.run([str(tool), "--version"], capture_output=True, text=True,
                             env=environment, check=True, timeout=30).stdout.strip()
    records = {}
    total_output = 0
    for level in ("O0", "Os"):
        source_dir = capture / f"{args.compiler}-{level}"
        target_dir = output / f"{args.compiler}-{level}"
        target_dir.mkdir()
        records[level] = {}
        for name in sorted(objects[level]):
            obj = source_dir / name
            details = object_details(obj)
            disassembly = target_dir / f"{name}.objdump"
            diagnostics = target_dir / f"{name}.stderr"
            relative_obj = Path(f"{args.compiler}-{level}") / name
            command = [str(tool), "-d", "-r", "-t", "-w", str(relative_obj)]
            with disassembly.open("xb") as stdout, diagnostics.open("xb") as stderr:
                result = subprocess.run(command, cwd=capture, stdout=stdout, stderr=stderr,
                                        env=environment,
                                        check=False, timeout=30,
                                        preexec_fn=limit_output)
            total_output += disassembly.stat().st_size + diagnostics.stat().st_size
            if result.returncode or diagnostics.stat().st_size or total_output > OUTPUT_LIMIT:
                raise ValueError(f"objdump failed, emitted diagnostics, or exceeded budget: {name}")
            details.update({"object_sha256": digest(obj), "objdump_sha256": digest(disassembly),
                            "command": command})
            records[level][name] = details
    for level, row in selected.items():
        identity.verify_artifacts(capture / f"{args.compiler}-{level}", row["artifacts_sha256"])
    if (any(digest(path) != hashes[name] for name, path in inputs.items()) or
            digest(tool) != tool_hash):
        raise ValueError("capture metadata or objdump changed during analysis")
    object_deltas = []
    function_deltas = []
    for name in sorted(objects["O0"]):
        debug, size = records["O0"][name], records["Os"][name]
        object_deltas.append({"object": name, "O0_exec_bytes": debug["exec_bytes"],
                              "Os_exec_bytes": size["exec_bytes"],
                              "exec_delta": debug["exec_bytes"] - size["exec_bytes"]})
        debug_functions, size_functions = unique_functions(debug), unique_functions(size)
        for function in sorted(debug_functions.keys() & size_functions.keys()):
            function_deltas.append({"object": name, "name": function,
                                    "O0_size": debug_functions[function],
                                    "Os_size": size_functions[function],
                                    "delta": debug_functions[function] - size_functions[function]})
    object_deltas.sort(key=lambda item: (-item["exec_delta"], item["object"]))
    function_deltas.sort(key=lambda item: (-item["delta"], item["object"], item["name"]))
    result = {"schema": 1, "compiler": args.compiler, "input_sha256": hashes,
              "objdump": {"path": str(tool), "sha256": tool_hash, "version": version},
              "records": records, "object_deltas": object_deltas,
              "function_deltas": function_deltas,
              "limitations": ["object executable sections are not linked flash savings",
                              "matched symbol sizes can reflect inlining and placement",
                              "stack reports are frames, not complete stack peaks"]}
    (output / "manifest.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                                           encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--compiler", required=True, choices=("gcc16", "arm13", "clang"))
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        analyze(args)
        return 0
    except (OSError, ValueError, ELFError, json.JSONDecodeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, KeyError, TypeError) as error:
        print(f"compiler-object attribution: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
