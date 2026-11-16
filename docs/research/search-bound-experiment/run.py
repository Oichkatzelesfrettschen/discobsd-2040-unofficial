"""Replay the unshipped tableless search experiment and its negative controls."""

import argparse
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-O2"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
CONTROLS = [
    ("skip-left", "if (k <= mem)", "if (k <= l)", 1, "oracle:"),
    ("skip-candidate", "h += k - ms;", "return 0;", 1, "oracle:"),
    ("counter-disabled", "search_comparisons++", "(void)0", 3, "counter:"),
]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def command(work, name, arguments, expected=0, diagnostic=None):
    result = subprocess.run(arguments, capture_output=True, text=True,
                            timeout=180, check=False)
    (work / (name + ".json")).write_text(json.dumps({
        "arguments": arguments, "returncode": result.returncode,
        "stdout": result.stdout, "stderr": result.stderr,
    }, indent=2) + "\n")
    require(result.returncode == expected,
            f"{name}: exit {result.returncode}, expected {expected}: " + result.stderr)
    if diagnostic is None:
        require(not result.stderr, name + ": unexpected diagnostics: " + result.stderr)
    else:
        require(diagnostic in result.stderr, name + ": named rejection is missing")
    return result.stdout


def compile_case(work, name, compiler, flags, source):
    executable = work / name
    command(work, name + "-compile", compiler + flags + ["-DCOUNT_COMPARISONS",
            str(source), str(HERE / "fixture.c"), "-o", str(executable)])
    return executable


def run_matrix(work, label, compiler, flags):
    good = compile_case(work, label, compiler, flags, HERE / "tableless.c")
    output = command(work, label + "-execute", [str(good)])
    require("4239266 earliest-match and comparison-bound cases pass" in output,
            label + ": the complete host matrix is missing")
    original = (HERE / "tableless.c").read_text()
    for control, before, after, expected, diagnostic in CONTROLS:
        require(original.count(before) == 1, control + ": mutation anchor differs")
        source = work / (control + ".c")
        source.write_text(original.replace(before, after))
        executable = compile_case(work, label + "-" + control, compiler, flags, source)
        command(work, label + "-" + control + "-execute", [str(executable)],
                expected, diagnostic)
    print(label + ": 4239266 cases pass; three controls reject", flush=True)


def arm_matrix(work, compiler):
    support = work / "support.o"
    command(work, "arm-support", compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra",
            "-Werror", "-fomit-frame-pointer", "-c",
            str(ROOT / "tests/libbn/qemu_support.c"), "-o", str(support)])
    program = work / "arm"
    command(work, "arm-compile", compiler + FLAGS + ARM + ["-DCOUNT_COMPARISONS",
            str(HERE / "tableless.c"), str(HERE / "fixture.c"), str(support),
            "-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc", "-o", str(program)])
    output = command(work, "arm-execute", ["qemu-arm", "-cpu", "cortex-m0", str(program)])
    require("4206113 earliest-match and comparison-bound cases pass" in output,
            "ARM: the complete matrix is missing")
    original = (HERE / "tableless.c").read_text()
    for control, before, after, expected, diagnostic in CONTROLS:
        require(original.count(before) == 1, control + ": mutation anchor differs")
        source = work / ("arm-" + control + ".c")
        source.write_text(original.replace(before, after))
        executable = work / ("arm-" + control)
        command(work, "arm-" + control + "-compile", compiler + FLAGS + ARM +
                ["-DCOUNT_COMPARISONS", str(source), str(HERE / "fixture.c"),
                 str(support), "-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc",
                 "-o", str(executable)])
        command(work, "arm-" + control + "-execute",
                ["qemu-arm", "-cpu", "cortex-m0", str(executable)], expected, diagnostic)
    command(work, "arm-object", compiler + FLAGS + ARM + ["-fstack-usage", "-c",
            str(HERE / "tableless.c"), "-o", str(work / "tableless.o")])
    print("ARMv6-M QEMU: 4206113 cases pass; three controls reject; guard pages stay host-only",
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True,
                        help="fresh evidence-directory label under .build-logs")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--ilp32-cc")
    parser.add_argument("--sanitizer-cc")
    parser.add_argument("--arm-cc")
    args = parser.parse_args()
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.label) is None:
        parser.error("label must contain only letters, digits, dots, underscores and hyphens")
    evidence_root = ROOT / ".build-logs"
    if evidence_root.is_symlink():
        parser.error("evidence root must be a local directory")
    work = evidence_root / ("search-bound-" + args.label)
    if work.exists() or work.is_symlink():
        parser.error("evidence directory exists")
    work.mkdir(parents=True)
    receipt = {"state": "running", "variants": {}, "sources": {}}
    for path in [HERE / "tableless.c", HERE / "fixture.c", Path(__file__)]:
        receipt["sources"][str(path.relative_to(ROOT))] = hashlib.sha256(
            path.read_bytes()).hexdigest()
    try:
        guards = ["-DGUARD_PAGES", "-D_GNU_SOURCE", "-D_DARWIN_C_SOURCE"]
        variants = [("native", args.cc, []), ("ilp32", args.ilp32_cc, ["-m32"]),
                    ("sanitized", args.sanitizer_cc,
                     ["-O1", "-g", "-fsanitize=address,undefined",
                      "-fno-omit-frame-pointer"])]
        for label, selected, extra in variants:
            if selected is None:
                receipt["variants"][label] = "not run: compiler not selected"
                continue
            compiler = shlex.split(selected)
            require(bool(compiler), label + ": empty compiler")
            command(work, label + "-version", compiler + ["--version"])
            run_matrix(work, label, compiler, FLAGS + extra + guards)
            receipt["variants"][label] = "pass"
        if args.arm_cc is not None:
            compiler = shlex.split(args.arm_cc)
            require(bool(compiler), "ARM: empty compiler")
            command(work, "arm-version", compiler + ["--version"])
            command(work, "qemu-version", ["qemu-arm", "--version"])
            arm_matrix(work, compiler)
            receipt["variants"]["arm"] = "pass"
        else:
            receipt["variants"]["arm"] = "not run: compiler not selected"
        receipt["state"] = "completed"
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        receipt["state"] = "failed"
        receipt["error"] = str(error)
        raise SystemExit(str(error)) from error
    finally:
        (work / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
