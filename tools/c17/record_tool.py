"""Retain opt-in executed tool, dependency, output and link-map evidence.

The wrapper forwards argv directly without a shell. Capture files belong to
one invocation directory; successful execution and complete capture are both
required before a command reports success.
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path


class CaptureError(ValueError):
    """A tool invocation cannot establish the declared capture boundary."""


def digest(path):
    state = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            state.update(chunk)
    return state.hexdigest()


def named_path(path, root):
    resolved = path.resolve()
    try:
        return resolved.relative_to(root).as_posix()
    except ValueError:
        return "external:" + str(resolved)


def file_record(path, root, records=None, retained_name=None):
    path = path.resolve()
    if not path.is_file():
        raise CaptureError(f"required file is absent: {path}")
    result = {"path": named_path(path, root), "sha256": digest(path),
              "bytes": path.stat().st_size}
    if retained_name is not None:
        retained_name = "artifacts/" + result["sha256"]
        retained = records / retained_name
        retained.parent.mkdir(parents=True, exist_ok=True)
        if not retained.exists():
            pending = retained.with_name(retained.name + "." + uuid.uuid4().hex)
            shutil.copyfile(path, pending)
            if digest(pending) != result["sha256"]:
                raise CaptureError(f"file changed during capture: {path}")
            pending.replace(retained)
        if digest(retained) != result["sha256"]:
            raise CaptureError(f"retained file changed: {retained}")
        result["retained"] = retained_name
    return result


def option_value(arguments, flag):
    positions = [index for index, argument in enumerate(arguments) if argument == flag]
    if positions:
        position = positions[-1]
        if position + 1 >= len(arguments):
            raise CaptureError(f"missing value after {flag}")
        return arguments[position + 1]
    if flag in {"-MF", "-o"}:
        values = [argument[len(flag):] for argument in arguments
                  if argument.startswith(flag) and argument != flag]
        if values:
            return values[-1]
    return None


def dependency_paths(path, directory, require_exists=True):
    text = path.read_text().replace("\\\n", " ")
    paths = set()
    for line in text.splitlines():
        if not line.strip():
            continue
        fields = re.split(r"(?<!\\):", line, maxsplit=1)
        if len(fields) != 2:
            raise CaptureError("malformed compiler dependency rule")
        for token in shlex.split(fields[1], comments=False):
            if token == "<stdin>":
                continue
            dependency = (directory / token.replace("$$", "$" )).resolve()
            if require_exists and not dependency.is_file():
                raise CaptureError(f"missing compiler dependency: {dependency}")
            paths.add(dependency)
    if not paths:
        raise CaptureError("compiler dependency file contains no input files")
    return sorted(paths)


def classify(arguments):
    tool_name = Path(arguments[0]).name
    if tool_name == "ar" or tool_name.endswith("-ar"):
        if len(arguments) < 3:
            return "query", [], None
        operation = arguments[1].lstrip("-")
        if any(character in operation for character in "rq"):
            return "archive", [], arguments[2]
        return "archive-read", [], None
    if tool_name in {"elf2aout", "boot2sum"}:
        if len(arguments) != 3:
            raise CaptureError("elf2aout requires one input and one output")
        return "transform", [], arguments[2]
    if tool_name == "objcopy" or tool_name.endswith("-objcopy"):
        if len(arguments) < 2:
            return "query", [], None
        return "transform", [], arguments[-1]
    if tool_name == "ranlib" or tool_name.endswith("-ranlib"):
        return "archive-index", [], arguments[-1] if len(arguments) > 1 else None
    compiler = tool_name in {"cc", "gcc", "clang"} or tool_name.endswith(("-gcc", "-clang"))
    linker = tool_name == "ld" or tool_name.endswith("-ld")
    value_options = {"-o", "-MF", "-MT", "-MQ", "-I", "-isystem", "-include",
                     "-imacros", "-x", "-B", "-L", "-l", "-T", "--sysroot",
                     "-isysroot", "-D", "-U", "-idirafter", "-iquote"}
    operands = []
    consume_value = False
    for argument in arguments[1:]:
        if consume_value:
            consume_value = False
        elif argument in value_options:
            consume_value = True
        elif argument == "-" or not argument.startswith("-"):
            operands.append(argument)
    sources = [argument for argument in operands
               if argument == "-" or argument.endswith((".c", ".S", ".s", ".i"))]
    if not compiler and not linker:
        return "tool", sources, option_value(arguments, "-o")
    output = option_value(arguments, "-o")
    if compiler and sources:
        if "-E" in arguments:
            return "preprocess", sources, output
        if "-fsyntax-only" in arguments:
            return "syntax", sources, None
        if "-c" in arguments or "-S" in arguments:
            if len(sources) != 1:
                raise CaptureError("one source per captured compile is required")
            suffix = ".s" if "-S" in arguments else ".o"
            if output is None:
                if sources[0] == "-":
                    raise CaptureError("stdin compilation requires an explicit output")
                output = Path(sources[0]).stem + suffix
            return "compile", sources, output
        return "compile-link", sources, output or "a.out"
    if linker or (compiler and (output or any(arg.endswith((".o", ".a")) for arg in arguments))):
        return "link", [], output or "a.out"
    return "query", [], None


def existing_map(arguments):
    for index, argument in enumerate(arguments):
        if argument.startswith(("-Map=", "--Map=")):
            return argument.split("=", 1)[1]
        if argument in {"-Map", "--Map"}:
            if index + 1 >= len(arguments):
                raise CaptureError("link map option lacks a destination")
            return arguments[index + 1]
        if argument.startswith("-Wl,-Map,"):
            return argument[len("-Wl,-Map,"):]
        if argument.startswith("-Wl,-Map="):
            return argument[len("-Wl,-Map="):]
    return None


def linker_scripts(arguments):
    tokens = []
    for argument in arguments[1:]:
        tokens.extend(argument[4:].split(",") if argument.startswith("-Wl,") else [argument])
    scripts = []
    for index, token in enumerate(tokens):
        if token in {"-T", "--script"}:
            if index + 1 >= len(tokens):
                raise CaptureError("linker script option lacks a path")
            scripts.append(tokens[index + 1])
        elif token.startswith("--script="):
            scripts.append(token.split("=", 1)[1])
        elif token.startswith("-T") and len(token) > 2:
            scripts.append(token[2:])
    return scripts


def map_archives(path, directory):
    text = path.read_text(errors="replace")
    names = set(re.findall(r"([^\s()]+\.a)\([^()]+\)", text))
    names.update(re.findall(r"^LOAD\s+(.+\.a)\s*$", text, re.MULTILINE))
    return sorted({(directory / name).resolve() for name in names})


def record_command(root, records, arguments, stdout_file=None):
    if not arguments or any(argument.startswith("@") for argument in arguments):
        raise CaptureError("response input and empty commands are unsupported")
    root = root.resolve()
    records = records.resolve()
    directory = Path.cwd().resolve()
    directory.relative_to(root)
    records.mkdir(parents=True, exist_ok=True)
    identity = str(time.time_ns()) + "-" + uuid.uuid4().hex
    tool = shutil.which(arguments[0])
    if tool is None:
        raise CaptureError(f"tool is unavailable: {arguments[0]}")
    kind, sources, output = classify(arguments)
    if stdout_file is not None:
        require_stdout = kind == "preprocess" and output is None
        if not require_stdout:
            raise CaptureError("redirected output capture requires preprocessing without -o")
        output = str(stdout_file)
    if output is not None:
        (directory / output).resolve().relative_to(root)
    receipt = {
        "schema_version": 1, "id": identity, "kind": kind,
        "directory": named_path(directory, root), "argv": arguments,
        "tool": file_record(Path(tool), root), "sources": [],
        "source": None, "inputs": [], "dependencies": [], "outputs": [],
        "exit_code": None, "capture_error": None, "linked_archives": [],
        "execution_invocation": os.environ.get("_DISCOBSD_EXECUTION_OWNERSHIP_INVOCATION"),
        "source_snapshot_sha256": (digest(Path(os.environ["C17_SOURCE_SNAPSHOT"]))
                                   if os.environ.get("C17_SOURCE_SNAPSHOT") else None),
    }
    stdin_bytes = None
    if "-" in sources:
        stdin_bytes = sys.stdin.buffer.read()
        stdin_name = identity + ".stdin"
        (records / stdin_name).write_bytes(stdin_bytes)
        receipt["stdin_sha256"] = hashlib.sha256(stdin_bytes).hexdigest()
        receipt["stdin_retained"] = stdin_name
    for source in sources:
        if source != "-":
            receipt["sources"].append(file_record(
                directory / source, root, records, identity + ".source"))
    if len(receipt["sources"]) == 1:
        receipt["source"] = receipt["sources"][0]
    input_names = [argument for argument in arguments[1:]
                   if not argument.startswith("-") and (directory / argument).is_file()
                   and (argument != output or kind in {"transform", "archive-index", "archive"})
                   and argument not in sources]
    if kind in {"link", "compile-link"}:
        input_names.extend(linker_scripts(arguments))
    for name in dict.fromkeys(input_names):
        input_path = directory / name
        if name in linker_scripts(arguments) and re.search(
                r"^\s*INCLUDE\b", input_path.read_text(), re.MULTILINE):
            raise CaptureError("nested linker INCLUDE requires an explicit capture adapter")
        receipt["inputs"].append(file_record(input_path, root, records, identity + ".input"))
    executed = list(arguments)
    dependency_file = None
    if sources and kind in {"compile", "compile-link", "preprocess", "syntax"}:
        dependency_value = option_value(arguments, "-MF")
        if dependency_value is not None:
            dependency_file = directory / dependency_value
        else:
            dependency_file = records / (identity + ".d")
            executed.extend(["-MD", "-MF", str(dependency_file)])
    map_file = None
    if kind in {"link", "compile-link"}:
        map_value = existing_map(arguments)
        if map_value is not None:
            map_file = directory / map_value
        else:
            map_file = records / (identity + ".map")
            tool_name = Path(arguments[0]).name
            if tool_name == "ld" or tool_name.endswith("-ld"):
                executed.append("-Map=" + str(map_file))
            else:
                executed.append("-Wl,-Map," + str(map_file))
    receipt["executed_argv"] = executed
    result = subprocess.run(executed, input=stdin_bytes, check=False)
    receipt["exit_code"] = result.returncode
    if result.returncode == 0:
        try:
            if dependency_file is not None:
                receipt["dependencies"] = [file_record(path, root, records, identity + ".input")
                                           for path in dependency_paths(dependency_file, directory)]
                receipt["dependency_file"] = file_record(
                    dependency_file, root, records, identity + ".dependencies")
            for original, source in zip((item for item in sources if item != "-"),
                                        receipt["sources"]):
                if digest(directory / original) != source["sha256"]:
                    raise CaptureError("source changed during compiler execution")
            if output is not None:
                receipt["outputs"].append(file_record(
                    directory / output, root, records, identity + ".output"))
            if map_file is not None:
                receipt["link_map"] = file_record(
                    map_file, root, records, identity + ".link-map")
                receipt["linked_archives"] = [file_record(
                    archive, root, records, identity + ".archive")
                    for archive in map_archives(map_file, directory)]
                before_archives = {row["path"]: row["sha256"] for row in receipt["inputs"]
                                   if row["path"].endswith(".a")}
                for archive in receipt["linked_archives"]:
                    if (archive["path"] in before_archives and
                            before_archives[archive["path"]] != archive["sha256"]):
                        raise CaptureError("archive changed during linker execution")
        except (OSError, ValueError) as error:
            receipt["capture_error"] = str(error)
    target = records / (identity + ".json")
    temporary = target.with_suffix(".pending")
    temporary.write_text(json.dumps(receipt, indent=2) + "\n")
    temporary.replace(target)
    if receipt["capture_error"] is not None:
        raise CaptureError(receipt["capture_error"])
    return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--stdout-file", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        return record_command(args.root, args.records, command, args.stdout_file)
    except (OSError, ValueError) as error:
        print(f"tool capture: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
