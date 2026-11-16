"""Capture clean RP2040 compiler matrix rows under the product ownership lock."""

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
from pathlib import Path

import check_kernel_headroom
import compiler_matrix_identity
import run_logged_gate

LEVELS = ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz")
COMPILERS = ("gcc16", "arm13", "clang")
RUNNER = Path(__file__).resolve().with_name("run_logged_gate.py")


def require_product_ownership(root):
    lock_path = root / ".build-logs/rp2040-products.lock"
    expected_path = run_logged_gate.execution_ownership_path(lock_path).resolve()
    path_value = os.environ.get(run_logged_gate.OWNERSHIP_PATH_ENV)
    invocation = os.environ.get(run_logged_gate.OWNERSHIP_INVOCATION_ENV)
    if not path_value or not invocation or Path(path_value).resolve() != expected_path:
        raise ValueError("matrix execution requires matching product ownership")
    deadline = time.monotonic() + 2.0
    while True:
        record = run_logged_gate.read_execution_ownership(expected_path)
        if (record is None or record["state"] != "active" or
                record["invocation_id"] != invocation):
            raise ValueError("matrix product ownership is absent or inactive")
        if os.getpgrp() in record["process_groups"]:
            return
        if not record["pending_launches"] or time.monotonic() >= deadline:
            raise ValueError("matrix process group lacks product ownership")
        time.sleep(0.01)


def tool_identity(path):
    path = path.resolve(strict=True)
    if not path.is_file():
        raise ValueError(f"tool input is not a regular file: {path}")
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def build_environment(root):
    environment = {"PATH": os.environ.get("PATH", os.defpath), "LC_ALL": "C",
                   "TZ": "UTC", "PYTHON": sys.executable}
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch is None:
        epoch = subprocess.run(
            ["git", "-c", "core.fsmonitor=false", "show", "-s", "--format=%ct", "HEAD"],
            cwd=root, capture_output=True, text=True, check=True, env=environment,
        ).stdout.strip()
    if not epoch.isascii() or not epoch.isdecimal():
        raise ValueError("SOURCE_DATE_EPOCH must be a nonnegative decimal timestamp")
    environment["SOURCE_DATE_EPOCH"] = epoch
    return environment


def input_identities(args, environment):
    paths = {"bmake": args.bmake, "clang": args.clang,
             "clang_libgcc": args.clang_libgcc}
    for name, prefix in (("gcc16", args.gcc16_prefix), ("arm13", args.arm13_prefix)):
        for suffix in ("gcc", "ld", "objcopy", "size", "objdump", "nm"):
            paths[f"{name}_{suffix}"] = Path(f"{prefix}-{suffix}")
        for program in ("cc1", "as", "ld", "collect2"):
            selected = subprocess.run(
                [f"{prefix}-gcc", f"-print-prog-name={program}"],
                capture_output=True, text=True, check=True, env=environment,
            ).stdout.strip()
            selected_path = selected if Path(selected).is_absolute() else shutil.which(
                selected, path=environment["PATH"]
            )
            if not selected_path:
                raise ValueError(f"{name} did not resolve its {program}")
            key = f"{name}_driver_ld" if program == "ld" else f"{name}_{program}"
            paths[key] = Path(selected_path)
        runtime = subprocess.run(
            [f"{prefix}-gcc", "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft",
             "-print-libgcc-file-name"], capture_output=True, text=True, check=True,
            env=environment,
        ).stdout.strip()
        if not runtime or not Path(runtime).is_absolute():
            raise ValueError(f"{name} did not resolve an absolute runtime archive")
        paths[f"{name}_libgcc"] = Path(runtime)
    clang_ld = subprocess.run(
        [str(args.clang), "--target=arm-none-eabi", "-print-prog-name=ld"],
        capture_output=True, text=True, check=True, env=environment,
    ).stdout.strip()
    clang_ld_path = clang_ld if Path(clang_ld).is_absolute() else shutil.which(
        clang_ld, path=environment["PATH"]
    )
    if not clang_ld_path:
        raise ValueError("Clang did not resolve its linker")
    paths["clang_ld"] = Path(clang_ld_path)
    for name in ("sh", "git", "awk", "basename", "cat", "cmp", "date", "expr",
                 "mkdir", "mv", "rm", "sed", "uname"):
        selected = shutil.which(name, path=environment["PATH"])
        if not selected:
            raise ValueError(f"required host utility is missing: {name}")
        paths[f"host_{name}"] = Path(selected)
    picotool = shutil.which("picotool", path=environment["PATH"])
    if picotool:
        paths["picotool"] = Path(picotool)
    return {name: tool_identity(path) for name, path in paths.items()}


def check_inputs(args, expected, tool_inputs, environment):
    current = compiler_matrix_identity.snapshot(
        args.root, args.generated_input, args.host_tool
    )
    if current != expected:
        raise ValueError("tracked, generated, or host-tool input changed")
    if input_identities(args, environment) != tool_inputs:
        raise ValueError("compiler, linker, runtime, or build tool changed")
    allowed = set(expected["tracked"]) | set(expected["generated"])
    source_root = args.root / "sys"
    for extension in ("*.h", "*.inc", "*.def"):
        for path in source_root.rglob(extension):
            if path.is_file() and path.relative_to(args.root).as_posix() not in allowed:
                raise ValueError(f"undeclared include-search input: {path}")


def invoke(command, cwd, log, environment):
    with log.open("xb") as stream:
        return subprocess.run(command, cwd=cwd, stdout=stream,
                              stderr=subprocess.STDOUT, check=False,
                              env=environment).returncode


def diagnostic_errors(text):
    """Count error-labelled lines and non-warning GNU linker diagnostics."""
    linker = re.compile(r"^(?:\S*/)?(?:arm-none-eabi-)?ld(?:\.[^:\s]+)?:\s*(.*)$")
    count = 0
    for line in text.splitlines():
        match = linker.match(line)
        if re.search(r"\berror:", line):
            count += 1
        elif (match and not re.search(r"\b(?:warning|note):", match.group(1)) and
              ": in function " not in match.group(1)):
            count += 1
    return count


def audit_dependencies(root, build_dir, expected):
    allowed = {
        (root / name).resolve(strict=True)
        for name in (*expected["tracked"], *expected["generated"])
    }
    for name in ("boot2.S", "vers.c"):
        generated = build_dir / name
        if generated.is_file():
            allowed.add(generated.resolve(strict=True))
    dependency_dir = build_dir / ".deps"
    if not dependency_dir.exists():
        return
    for dependency_file in sorted(dependency_dir.glob("*.dep")):
        content = dependency_file.read_text(encoding="utf-8")
        first_rule = content.replace("\\\n", " ").split("\n", 1)[0]
        if ":" not in first_rule:
            raise ValueError(f"malformed compiler dependency file: {dependency_file}")
        dependencies = shlex.split(first_rule.split(":", 1)[1])
        if not dependencies:
            raise ValueError(f"empty compiler dependency file: {dependency_file}")
        for dependency in dependencies:
            resolved = (build_dir / dependency).resolve(strict=True)
            if resolved not in allowed:
                raise ValueError(f"undeclared compiler dependency: {resolved}")


def capture_stack_usage(build_dir, row_dir, nm, environment):
    """Retain reports or prove that an unreported C object has no code symbol."""
    expected_reports = set()
    dependency_dir = build_dir / ".deps"
    if dependency_dir.is_dir():
        for dependency_file in dependency_dir.glob("*.dep"):
            first_rule = dependency_file.read_text(encoding="utf-8").replace(
                "\\\n", " "
            ).split("\n", 1)[0]
            dependencies = shlex.split(first_rule.split(":", 1)[1])
            if (dependencies and dependencies[0].endswith(".c") and
                    (build_dir / (dependency_file.stem + ".o")).is_file()):
                expected_reports.add(dependency_file.stem + ".su")
    if (build_dir / "vers.o").is_file():
        expected_reports.add("vers.su")
    actual_reports = {path.name for path in build_dir.glob("*.su")}
    unexpected = actual_reports - expected_reports
    if unexpected:
        raise ValueError(f"unexpected stack-usage reports: {sorted(unexpected)}")
    no_code_objects = []
    for name in sorted(expected_reports - actual_reports):
        object_name = Path(name).with_suffix(".o").name
        symbols = subprocess.run([str(nm), "-P", "--defined-only", str(build_dir / object_name)],
                                 check=True, capture_output=True, text=True, env=environment)
        (row_dir / (object_name + ".symbols")).write_text(symbols.stdout, encoding="utf-8")
        for line in symbols.stdout.splitlines():
            fields = line.split()
            if len(fields) < 2 or len(fields[1]) != 1:
                raise ValueError(f"malformed nm output for {object_name}")
            if fields[1] in "tTwWi":
                raise ValueError(f"missing stack-usage report for code object {object_name}")
        no_code_objects.append(object_name)
    hashes = {}
    for name in sorted(actual_reports):
        report = build_dir / name
        hashes[name] = hashlib.sha256(report.read_bytes()).hexdigest()
        shutil.copy2(report, row_dir / name)
    return hashes, no_code_objects


def run_rows(args):
    root = args.root.resolve(strict=True)
    manifest_bytes = args.manifest.read_bytes()
    expected = json.loads(manifest_bytes)
    environment = build_environment(root)
    tool_inputs = input_identities(args, environment)
    check_inputs(args, expected, tool_inputs, environment)
    recorded_tools = {name: dict(identity) for name, identity in tool_inputs.items()}
    for name, identity in recorded_tools.items():
        if name.endswith(("libgcc", "cc1", "collect2")) or name.startswith("host_"):
            continue
        if name == "bmake":
            version_command = [identity["path"], "-V", "MAKE_VERSION"]
        elif name == "picotool":
            version_command = [identity["path"], "version"]
        else:
            version_command = [identity["path"], "--version"]
        version = subprocess.run(version_command, capture_output=True, text=True, check=True,
                                 env=environment)
        identity["version"] = (version.stdout + version.stderr).strip()
        if not identity["version"]:
            raise ValueError(f"tool did not report its version: {name}")
    check_inputs(args, expected, tool_inputs, environment)
    (args.output / "source-manifest.json").write_bytes(manifest_bytes)
    (args.output / "build-environment.json").write_text(
        json.dumps(environment, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (args.output / "tool-inputs.json").write_text(
        json.dumps(recorded_tools, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    build_dir = root / "sys/arch/rp2040/compile/PICO"
    if not build_dir.is_dir():
        raise ValueError("PICO build directory is missing")
    linker_script = root / "sys/arch/rp2040/conf/RP2040.ld"
    origin, length = check_kernel_headroom.region_bounds(
        linker_script.read_text(encoding="utf-8"), check_kernel_headroom.DEFAULT_REGION
    )
    rows = []
    failed = False
    version_sha256 = None
    selected_compilers = [name for name in COMPILERS if not args.compiler or name in args.compiler]
    selected_levels = [level for level in LEVELS if not args.level or level in args.level]
    for compiler in selected_compilers:
        for level in selected_levels:
            check_inputs(args, expected, tool_inputs, environment)
            row_dir = args.output / f"{compiler}-{level}"
            row_dir.mkdir()
            print(f"compiler-matrix: starting {compiler} -{level}", flush=True)
            base = [str(args.bmake), "-k", "-C", str(build_dir), "MACHINE=rp2040"]
            clean_command = [*base, "clean"]
            clean_exit = invoke(clean_command, root, row_dir / "clean.log", environment)
            if clean_exit == 0:
                stale = list(build_dir.glob("*.o"))
                stale.extend(build_dir.glob("*.su"))
                stale.extend(build_dir / name for name in
                             ("unix", "unix.elf", "unix.uf2", "unix.map", "boot2.S",
                              "boot2.elf", "boot2.bin", "vers.c", ".params")
                             if (build_dir / name).exists())
                if stale:
                    raise ValueError(f"clean left build products: {stale[0]}")
            command = [*base, f"COPTS=-{level}"]
            if args.stack_usage:
                command.append("DEBUG=-g -gdwarf-4 -fstack-usage")
            if compiler == "gcc16":
                command.append(f"ARM_GCC_PREFIX={args.gcc16_prefix}")
            elif compiler == "arm13":
                command.append(f"ARM_GCC_PREFIX={args.arm13_prefix}")
            else:
                command.extend([f"ARM_GCC_PREFIX={args.gcc16_prefix}",
                                f"CC={args.clang} --target=arm-none-eabi -ferror-limit=0",
                                f"AS={args.clang} --target=arm-none-eabi",
                                f"LIBGCC={args.clang_libgcc}"])
            command.append("all")
            build_exit = (
                invoke(command, root, row_dir / "build.log", environment)
                if clean_exit == 0 else 125
            )
            check_inputs(args, expected, tool_inputs, environment)
            if clean_exit == 0:
                audit_dependencies(root, build_dir, expected)
                if (build_dir / ".deps").is_dir():
                    shutil.copytree(build_dir / ".deps", row_dir / "deps")
            stack_usage, no_code_objects = None, None
            if clean_exit == 0 and args.stack_usage:
                prefix = args.arm13_prefix if compiler == "arm13" else args.gcc16_prefix
                stack_usage, no_code_objects = capture_stack_usage(
                    build_dir, row_dir, Path(f"{prefix}-nm"), environment
                )
            if clean_exit == 0 and build_exit == 0 and not (build_dir / "unix").is_file():
                raise ValueError(f"{compiler} -{level} succeeded without producing unix")
            if clean_exit == 0:
                for artifact in sorted(build_dir.glob("*.o")):
                    shutil.copy2(artifact, row_dir / artifact.name)
                for name in ("unix", "unix.elf", "unix.uf2", "unix.map", ".params",
                             "boot2.S", "boot2.elf", "boot2.bin", "vers.c"):
                    artifact = build_dir / name
                    if artifact.is_file():
                        shutil.copy2(artifact, row_dir / name)
            version_path = row_dir / "vers.c"
            row_version = (hashlib.sha256(version_path.read_bytes()).hexdigest()
                           if version_path.is_file() else None)
            if row_version is not None:
                if version_sha256 is None:
                    version_sha256 = row_version
                elif row_version != version_sha256:
                    raise ValueError(f"{compiler} -{level} produced a different version payload")
            log_text = (row_dir / "build.log").read_text(
                encoding="utf-8", errors="replace"
            ) if clean_exit == 0 else ""
            clean_text = (row_dir / "clean.log").read_text(
                encoding="utf-8", errors="replace"
            )
            clean_warnings = len(re.findall(r"\bwarning:", clean_text))
            build_warnings = len(re.findall(r"\bwarning:", log_text))
            clean_errors = diagnostic_errors(clean_text)
            build_errors = diagnostic_errors(log_text)
            warnings = clean_warnings + build_warnings
            errors = clean_errors + build_errors
            headroom_bytes = None
            headroom_pass = None
            headroom_error = None
            if build_exit == 0:
                try:
                    _, headroom_bytes = check_kernel_headroom.measure(
                        row_dir / "unix", origin, length
                    )
                    headroom_pass = check_kernel_headroom.within_margin(
                        headroom_bytes, check_kernel_headroom.DEFAULT_MARGIN
                    )
                except check_kernel_headroom.InputError as error:
                    headroom_pass = False
                    headroom_error = str(error)
            row = {"compiler": compiler, "level": level, "clean_command": clean_command,
                   "clean_exit": clean_exit, "build_command": command,
                   "build_exit": build_exit, "warnings": warnings, "errors": errors,
                   "headroom_bytes": headroom_bytes, "headroom_pass": headroom_pass,
                   "headroom_error": headroom_error,
                   "headroom_margin_bytes": check_kernel_headroom.DEFAULT_MARGIN,
                   "stack_usage_sha256": stack_usage,
                   "stack_usage_no_code_objects": no_code_objects,
                   "artifacts_sha256": compiler_matrix_identity.artifact_hashes(row_dir),
                   "version_sha256": row_version,
                   "clean_warnings": clean_warnings, "build_warnings": build_warnings,
                   "clean_errors": clean_errors, "build_errors": build_errors}
            rows.append(row)
            (args.output / "results.json").write_text(
                json.dumps(rows, indent=2) + "\n", encoding="utf-8"
            )
            failed |= (clean_exit != 0 or build_exit != 0 or warnings != 0 or
                       errors != 0 or headroom_pass is False)
            print(f"compiler-matrix: finished {compiler} -{level}: "
                  f"clean={clean_exit} build={build_exit} "
                  f"headroom={headroom_bytes} headroom_pass={headroom_pass}", flush=True)
    coverage = {"compilers": selected_compilers, "levels": selected_levels,
                "rows": len(rows),
                "complete_matrix": (len(rows) == len(COMPILERS) * len(LEVELS))}
    (args.output / "coverage.json").write_text(
        json.dumps(coverage, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--generated-input", action="append", default=[])
    parser.add_argument("--host-tool", action="append", default=[])
    parser.add_argument("--gcc16-prefix", required=True, type=Path)
    parser.add_argument("--arm13-prefix", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--clang-libgcc", required=True, type=Path)
    parser.add_argument("--bmake", required=True, type=Path)
    parser.add_argument("--compiler", choices=COMPILERS, action="append")
    parser.add_argument("--level", choices=LEVELS, action="append")
    parser.add_argument("--stack-usage", action="store_true")
    parser.add_argument("--owned", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        required_tools = {"tools/bin/rp2040/config", "tools/bin/rp2040/boot2sum"}
        if not required_tools.issubset(args.host_tool):
            raise ValueError("config and boot2sum must be named as host-tool inputs")
        if "sys/arch/rp2040/compile/PICO/ioconf.c" not in args.generated_input:
            raise ValueError("PICO ioconf.c must be named as a generated input")
        args.root = args.root.resolve(strict=True)
        args.manifest = args.manifest.resolve(strict=True)
        args.output = args.output.resolve()
        args.gcc16_prefix = args.gcc16_prefix.resolve()
        args.arm13_prefix = args.arm13_prefix.resolve()
        args.clang = args.clang.resolve(strict=True)
        args.clang_libgcc = args.clang_libgcc.resolve(strict=True)
        args.bmake = args.bmake.resolve(strict=True)
        if args.owned:
            require_product_ownership(args.root)
            return run_rows(args)
        expected = json.loads(args.manifest.read_text(encoding="utf-8"))
        environment = build_environment(args.root)
        tool_inputs = input_identities(args, environment)
        check_inputs(args, expected, tool_inputs, environment)
        args.output.mkdir(parents=False, exist_ok=False)
        child = [sys.executable, str(Path(__file__).resolve()),
                 "--root", str(args.root), "--manifest", str(args.manifest),
                 "--output", str(args.output), "--gcc16-prefix", str(args.gcc16_prefix),
                 "--arm13-prefix", str(args.arm13_prefix), "--clang", str(args.clang),
                 "--clang-libgcc", str(args.clang_libgcc), "--bmake", str(args.bmake)]
        for generated in args.generated_input:
            child.extend(["--generated-input", generated])
        for host_tool in args.host_tool:
            child.extend(["--host-tool", host_tool])
        for compiler in args.compiler or []:
            child.extend(["--compiler", compiler])
        for level in args.level or []:
            child.extend(["--level", level])
        if args.stack_usage:
            child.append("--stack-usage")
        command = [sys.executable, str(RUNNER), "--label", "rp2040-compiler-matrix",
                   "--evidence-root", str(args.output / "logged"),
                   "--execution-lock", str(args.root / ".build-logs/rp2040-products.lock"),
                   "--idle-timeout-seconds", "600", "--timeout-seconds", "3600",
                   "--", *child, "--owned"]
        return subprocess.run(command, cwd=args.root, check=False).returncode
    except (OSError, ValueError, json.JSONDecodeError, check_kernel_headroom.InputError,
            subprocess.CalledProcessError) as error:
        print(f"compiler-matrix capture: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
