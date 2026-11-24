"""Capture a source-bound lexical PICO graph and cscope index."""

import argparse
import functools
import hashlib
import json
import os
import re
import resource
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

import compiler_matrix_identity

CONFIG = Path("sys/arch/rp2040/compile/PICO")
GENERATED = tuple(str(CONFIG / name) for name in ("ioconf.c", "swapunix.c", "vers.c"))
VARIABLES = ("CFILES", "PARAM", "INCLUDES", "CFLAGS", "SYSTEM_OBJ", "LDADD")
OUTPUT_LIMIT = 64 * 1024 * 1024
TOOL_TIMEOUT = 120


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bound_output(limit):
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))


def build_cpp_command(args, config_dir, root, variables, defs_raw=""):
    """Build deterministic preprocessor invocation for --preprocess."""
    if not args.preprocess:
        return None
    cpp_bin = args.cpp
    if cpp_bin is None:
        located = shutil.which("arm-none-eabi-gcc") or shutil.which("cc")
        if not located:
            raise ValueError("--preprocess requires --cpp or arm-none-eabi-gcc/cc on PATH")
        cpp_bin = Path(located)
    cpp_path = cpp_bin.resolve(strict=True)
    flags = []
    seen = set()
    tokens = (
        shlex.split(defs_raw)
        + shlex.split(variables["PARAM"])
        + shlex.split(variables["INCLUDES"])
        + shlex.split(variables["CFLAGS"])
    )
    idx = 0
    while idx < len(tokens):
        tok = tokens[idx]
        item = None
        if tok == "-nostdinc":
            item = [tok]
        elif tok in ("-D", "-U") and idx + 1 < len(tokens):
            item = [tok, tokens[idx + 1]]
            idx += 1
        elif tok.startswith(("-D", "-U")):
            item = [tok]
        elif tok == "-I" and idx + 1 < len(tokens):
            inc_path = Path(os.path.normpath(config_dir / tokens[idx + 1]))
            rel = (
                inc_path.relative_to(root).as_posix()
                if inc_path.is_relative_to(root)
                else str(inc_path)
            )
            item = ["-I", rel]
            idx += 1
        elif tok.startswith("-I"):
            inc_path = Path(os.path.normpath(config_dir / tok[2:]))
            rel = (
                inc_path.relative_to(root).as_posix()
                if inc_path.is_relative_to(root)
                else str(inc_path)
            )
            item = [f"-I{rel}"]
        if item is not None:
            key = tuple(item)
            if key not in seen:
                seen.add(key)
                flags.extend(item)
        idx += 1
    return cpp_path, shlex.join([str(cpp_path), "-E", *flags])


def capture(args):
    root = args.root.resolve(strict=True)
    if not re.fullmatch(r"[A-Za-z0-9_]+", args.config):
        raise ValueError(f"invalid kernel config name: {args.config}")
    config_rel = Path("sys/arch/rp2040/compile") / args.config
    generated = tuple(str(config_rel / name) for name in ("ioconf.c", "swapunix.c", "vers.c"))
    source_manifest = args.manifest.read_bytes()
    expected = json.loads(source_manifest)
    current = compiler_matrix_identity.snapshot(root, generated, [])
    if current != expected:
        raise ValueError("source identity differs from the supplied manifest")
    config = root / config_rel
    environment = {
        "PATH": os.environ.get("PATH", os.defpath),
        "LC_ALL": "C",
        "TZ": "UTC",
        "PYTHON": sys.executable,
    }
    tools = {}
    for name in ("bmake", "cflow", "cscope"):
        program = getattr(args, name).resolve(strict=True)
        version_args = (
            [str(program), "-V", "MAKE_VERSION"]
            if name == "bmake"
            else [str(program), "-V" if name == "cscope" else "--version"]
        )
        result = subprocess.run(
            version_args, cwd=root, env=environment, check=True, text=True, capture_output=True
        )
        tools[name] = {
            "path": str(program),
            "sha256": digest(program),
            "version": (result.stdout + result.stderr).strip(),
        }
        if not tools[name]["version"]:
            raise ValueError(f"{name} did not report a version")
    # cflow 1.8 loads a default profile unless -q is given; cflow 1.7 has
    # neither the profile nor the option and exits 64 on it.
    profile_probe = subprocess.run(
        [str(args.cflow.resolve(strict=True)), "-q", "--version"],
        cwd=root,
        env=environment,
        check=False,
        capture_output=True,
    )
    cflow_profile = ["-q"] if profile_probe.returncode == 0 else []
    variables = {}
    for name in VARIABLES:
        command = [str(args.bmake), "-k", "MACHINE=rp2040", "-V", "${" + name + "}"]
        result = subprocess.run(
            command, cwd=config, env=environment, check=True, text=True, capture_output=True
        )
        if result.stderr:
            raise ValueError(f"bmake {name} emitted diagnostics: {result.stderr}")
        variables[name] = result.stdout.strip()
    defs_raw = ""
    if args.preprocess:
        defs_res = subprocess.run(
            [str(args.bmake), "-k", "MACHINE=rp2040", "-V", "${DEFS}"],
            cwd=config,
            env=environment,
            check=True,
            text=True,
            capture_output=True,
        )
        if defs_res.stderr:
            raise ValueError(f"bmake DEFS emitted diagnostics: {defs_res.stderr}")
        defs_raw = defs_res.stdout.strip()
    cpp_info = build_cpp_command(args, config, root, variables, defs_raw)
    if cpp_info is not None:
        cpp_path, cpp_cmd = cpp_info
        cpp_ver = subprocess.run(
            [str(cpp_path), "--version"],
            cwd=root,
            env=environment,
            check=True,
            text=True,
            capture_output=True,
        )
        tools["cpp"] = {
            "path": str(cpp_path),
            "sha256": digest(cpp_path),
            "version": (cpp_ver.stdout + cpp_ver.stderr).splitlines()[0].strip(),
        }
        cflow_profile.append(f"--cpp={cpp_cmd}")
    allowed = set(expected["tracked"]) | set(expected["generated"])
    sources = []
    for name in shlex.split(variables["CFILES"]) + ["ioconf.c", "vers.c"]:
        source = (config / name).resolve(strict=True)
        if not source.is_relative_to(root) or not source.is_file():
            raise ValueError(f"C source escapes repository or is not a file: {name}")
        relative = source.relative_to(root).as_posix()
        if relative not in allowed or not relative.endswith(".c"):
            raise ValueError(f"undeclared or non-C configured source: {relative}")
        if relative in sources:
            raise ValueError(f"duplicate configured source: {relative}")
        sources.append(relative)
    if not sources or not set(generated).issubset(sources):
        raise ValueError("configured source list lacks required generated inputs")
    headers = sorted(
        name for name in expected["tracked"] if name.startswith("sys/") and name.endswith(".h")
    )
    index_sources = sorted(set(sources + headers))
    if any(any(character.isspace() for character in name) for name in index_sources):
        raise ValueError("cscope input names containing whitespace are unsupported")
    args.output.mkdir(parents=False, exist_ok=False)
    (args.output / "source-manifest.json").write_bytes(source_manifest)
    (args.output / "cscope.files").write_text("\n".join(index_sources) + "\n", encoding="utf-8")
    commands = []
    deadline = time.monotonic() + TOOL_TIMEOUT

    def run_tool(name, command, stdout_name):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ValueError("graph capture exceeded its total deadline")
        commands.append(command)
        with (
            (args.output / stdout_name).open("xb") as stdout,
            (args.output / f"{name}.stderr").open("xb") as stderr,
        ):
            limit_output = functools.partial(bound_output, args.output_limit_bytes)
            result = subprocess.run(
                command,
                cwd=root,
                env=environment,
                check=False,
                stdout=stdout,
                stderr=stderr,
                timeout=remaining,
                preexec_fn=limit_output,
            )
        if result.returncode:
            raise ValueError(f"{name} failed with exit {result.returncode}")
        output_bytes = sum(
            path.stat().st_size
            for path in args.output.iterdir()
            if path.is_file() and path.name != "source-manifest.json"
        )
        if output_bytes > args.output_limit_bytes:
            raise ValueError("graph capture exceeded its aggregate output limit")

    discovered = set()
    for pass_number in range(128):
        selection = (
            ["--all"] if not discovered else [f"--start={name}" for name in sorted(discovered)]
        )
        graph_name = f"direct-pass-{pass_number:03d}.dot"
        command = [
            str(args.cflow),
            *cflow_profile,
            "--format=dot",
            "--depth=2",
            *selection,
            *sources,
        ]
        run_tool(f"cflow-{pass_number:03d}", command, graph_name)
        graph = (args.output / graph_name).read_text(encoding="utf-8")
        names = set(re.findall(r"(?m)^\s*([A-Za-z_][A-Za-z_0-9]*)\s+\[label=", graph))
        names.update(
            re.findall(
                r"(?m)^\s*[A-Za-z_][A-Za-z_0-9]*\s+->\s+"
                r"([A-Za-z_][A-Za-z_0-9]*)\s*$",
                graph,
            )
        )
        if not names:
            raise ValueError("cflow produced no graph symbols")
        if discovered - names:
            raise ValueError("cflow omitted a previously discovered start symbol")
        if names <= discovered:
            shutil.copy2(args.output / graph_name, args.output / "direct.dot")
            break
        discovered.update(names)
    else:
        raise ValueError("cflow direct-edge discovery did not converge")
    cscope_command = [
        str(args.cscope),
        "-b",
        "-q",
        "-k",
        "-i",
        str(args.output / "cscope.files"),
        "-f",
        str(args.output / "cscope.out"),
    ]
    run_tool("cscope", cscope_command, "cscope.stdout")
    if compiler_matrix_identity.snapshot(root, generated, []) != expected:
        raise ValueError("source identity changed during graph capture")
    for name, identity in tools.items():
        if digest(Path(identity["path"])) != identity["sha256"]:
            raise ValueError(f"{name} changed during graph capture")
    artifacts = {
        path.name: digest(path) for path in sorted(args.output.iterdir()) if path.is_file()
    }
    limitations = [
        "cscope indexes lexical references",
        "indirect calls and runtime reachability are not established",
    ]
    if not args.preprocess:
        limitations.insert(0, "cflow does not apply the build preprocessor configuration")
    result = {
        "schema": 1,
        "config": args.config,
        "preprocessed": args.preprocess,
        "source_manifest_sha256": hashlib.sha256(source_manifest).hexdigest(),
        "tools": tools,
        "configuration": variables,
        "c_sources": sources,
        "cscope_sources": index_sources,
        "commands": commands,
        "artifacts": artifacts,
        "cflow_passes": pass_number + 1,
        "tool_timeout_seconds": TOOL_TIMEOUT,
        "per_file_output_limit_bytes": args.output_limit_bytes,
        "limitations": limitations,
    }
    (args.output / "manifest.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "manifest", "output", "bmake", "cflow", "cscope"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(
        "--config",
        default="PICO",
        help="Kernel configuration directory under sys/arch/rp2040/compile",
    )
    parser.add_argument(
        "--preprocess",
        action="store_true",
        help="Pass configured PARAM/INCLUDES flags to cflow --cpp",
    )
    parser.add_argument(
        "--cpp", type=Path, default=None, help="Compiler driver for --preprocess -E"
    )
    parser.add_argument("--output-limit-bytes", type=int, default=OUTPUT_LIMIT)
    args = parser.parse_args()
    try:
        if not 0 < args.output_limit_bytes <= OUTPUT_LIMIT:
            raise ValueError("output limit must be positive and no greater than 64 MiB")
        args.manifest = args.manifest.resolve(strict=True)
        args.output = args.output.resolve()
        for name in ("bmake", "cflow", "cscope"):
            setattr(args, name, getattr(args, name).resolve(strict=True))
        capture(args)
        return 0
    except (
        OSError,
        ValueError,
        json.JSONDecodeError,
        subprocess.CalledProcessError,
        subprocess.TimeoutExpired,
    ) as error:
        print(f"callgraph capture: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
