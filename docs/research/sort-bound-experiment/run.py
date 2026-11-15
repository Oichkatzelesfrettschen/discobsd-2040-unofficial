"""Replay bidirectional equal-band sort behavior and comparison controls."""

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

BASELINE = "efb4a05a42f1e0c146625271b80ed0e9f1705212"
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-fno-builtin"]


def run(work, label, command, expected=0, diagnostic=None, marker=None):
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=180,
        env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"),
    )
    record = {
        "command": command,
        "exit": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }
    (work / (label + ".json")).write_text(json.dumps(record, indent=2) + "\n")
    if result.returncode != expected:
        raise RuntimeError(label + ": unexpected exit: " + str(record))
    if diagnostic is None and result.stderr:
        raise RuntimeError(label + ": unexpected diagnostics: " + result.stderr)
    if diagnostic is not None and diagnostic not in result.stderr:
        raise RuntimeError(label + ": named rejection is missing")
    if marker is not None and marker not in result.stdout:
        raise RuntimeError(label + ": execution marker is missing")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--source", type=Path, default=Path(__file__).with_name("bidirectional.c"))
    parser.add_argument("--counts", type=Path, default=Path(__file__).with_name("counts.c"))
    parser.add_argument("--arm", action="store_true", help="execute ARMv6-M through QEMU")
    args = parser.parse_args()
    root = args.root.resolve()
    evidence = root / ".build-logs"
    evidence.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="bidirectional-sort-", dir=evidence))
    source = args.source.read_text()
    counts = args.counts.read_text()
    (work / "counts.c").write_text(counts)
    baseline = run(
        work,
        "baseline-source",
        ["git", "-C", str(root), "show", BASELINE + ":lib/libc/gen/qsort.c"],
    )
    (work / "baseline.c").write_text(baseline)
    fixture = run(
        work,
        "fixture-source",
        ["git", "-C", str(root), "show", BASELINE + ":tests/libc_contracts/qsort_context_test.c"],
    )
    if fixture.count("unsigned char keys[8];") != 1:
        raise RuntimeError("exhaustive fixture anchor differs")
    fixture = fixture.replace("unsigned char keys[8];", "unsigned char keys[10];")
    (work / "fixture.c").write_text(fixture)
    (work / "candidate.c").write_text(source)
    anchor = "    sort(base, count, width, compare, context, budget);"
    if source.count(anchor) != 1:
        raise RuntimeError("entry anchor differs")
    (work / "heap.c").write_text(source.replace(anchor, "    budget=0;\n" + anchor))
    profiles = [
        ("host", shlex.split(os.environ.get("CC", "cc")), ["-O2"]),
        ("ilp32", ["cc", "-m32"], ["-O2"]),
        (
            "sanitizer",
            ["clang"],
            ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"],
        ),
    ]
    marker = "qsort context: 708596 order, permutation and context cases pass"
    for name, compiler, options in profiles:
        for variant in ("candidate", "heap"):
            objects = []
            for mode in ("plain", "context"):
                obj = work / (name + "-" + variant + "-" + mode + ".o")
                run(
                    work,
                    obj.stem,
                    compiler
                    + FLAGS
                    + options
                    + (["-DCONTEXT"] if mode == "context" else [])
                    + ["-c", str(work / (variant + ".c")), "-o", str(obj)],
                )
                objects.append(str(obj))
            binary = work / (name + "-" + variant)
            run(
                work,
                binary.name + "-link",
                compiler + FLAGS + options + [str(work / "fixture.c"), *objects, "-o", str(binary)],
            )
            run(work, binary.name + "-run", [str(binary)], marker=marker)
        print("PASS " + name + ": partition and forced heap", flush=True)
    mutations = [
        (
            "order",
            "    (void)sort; (void)budget;\n    for (size_t i=1;i<count;i++) "
            "(void)CMP((unsigned char *)base+(i-1)*width,(unsigned char *)base+i*width);",
            "output violates requested order",
        ),
        (
            "context",
            "    (void)context;\n    sort(base,count,width,compare,NULL,budget);",
            "callback loses context",
        ),
        (
            "multiplicity",
            anchor + "\n    ((unsigned char *)base)[(count-1)*width]=255;",
            "key multiplicities change",
        ),
    ]
    mutation_sources = [
        (name, source.replace(anchor, replacement), diagnostic)
        for name, replacement, diagnostic in mutations
    ]
    heap_anchor = "            heap(base, count, width, compare, context);"
    if source.count(heap_anchor) != 1:
        raise RuntimeError("heap mutation anchor differs")
    mutation_sources.append(
        (
            "fallback",
            (work / "heap.c")
            .read_text()
            .replace(
                heap_anchor,
                "            (void)heap;\n"
                "            for (size_t i=1;i<count;i++) "
                "(void)CMP(base+(i-1)*width,base+i*width);",
            ),
            "output violates requested order",
        )
    )
    for name, content, diagnostic in mutation_sources:
        mutant = work / (name + ".c")
        mutant.write_text(content)
        obj = work / (name + ".o")
        run(
            work,
            name + "-compile",
            ["cc", *FLAGS, "-O2", "-DCONTEXT", "-c", str(mutant), "-o", str(obj)],
        )
        binary = work / name
        run(
            work,
            name + "-link",
            [
                "cc",
                *FLAGS,
                "-O2",
                str(work / "fixture.c"),
                str(work / "host-candidate-plain.o"),
                str(obj),
                "-o",
                str(binary),
            ],
        )
        run(work, name + "-run", [str(binary)], 1, "qsort context: " + diagnostic)
        print("PASS control: " + name, flush=True)
    for name, provider, expected, diagnostic in (
        ("candidate-counts", work / "candidate.c", 0, None),
        ("baseline-counts", work / "baseline.c", 4, "comparison bound:"),
    ):
        obj = work / (name + ".o")
        run(
            work,
            name + "-compile",
            ["cc", *FLAGS, "-O2", "-Dqsort=db_qsort", "-c", str(provider), "-o", str(obj)],
        )
        binary = work / name
        run(
            work,
            name + "-link",
            ["cc", *FLAGS, "-O2", str(work / "counts.c"), str(obj), "-o", str(binary)],
        )
        run(
            work,
            name + "-run",
            [str(binary)],
            expected,
            diagnostic,
            "qsort counts: 56 pattern cases pass" if expected == 0 else None,
        )
    if counts.count("calls++;") != 1:
        raise RuntimeError("counter mutation anchor differs")
    counter = work / "counter-disabled.c"
    counter.write_text(counts.replace("calls++;", "(void)0;"))
    binary = work / "counter-disabled"
    run(
        work,
        "counter-disabled-compile",
        ["cc", *FLAGS, "-O2", str(counter), str(work / "candidate-counts.o"), "-o", str(binary)],
    )
    run(work, "counter-disabled-run", [str(binary)], 3, "comparison counter:")
    print("PASS control: comparison counter", flush=True)
    arm = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
    if args.arm:
        support = work / "support.o"
        run(
            work,
            "arm-support",
            [
                "arm-none-eabi-gcc",
                *arm,
                "-std=gnu17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fomit-frame-pointer",
                "-c",
                str(root / "tests/libbn/qemu_support.c"),
                "-o",
                str(support),
            ],
        )
        for variant in ("candidate", "heap"):
            objects = []
            for mode in ("plain", "context"):
                obj = work / ("arm-" + variant + "-" + mode + ".o")
                run(
                    work,
                    obj.stem,
                    [
                        "arm-none-eabi-gcc",
                        *FLAGS,
                        *arm,
                        "-fstack-usage",
                        *(["-DCONTEXT"] if mode == "context" else []),
                        "-c",
                        str(work / (variant + ".c")),
                        "-o",
                        str(obj),
                    ],
                )
                objects.append(str(obj))
            binary = work / ("arm-" + variant)
            run(
                work,
                binary.name + "-link",
                [
                    "arm-none-eabi-gcc",
                    *FLAGS,
                    *arm,
                    "-nostartfiles",
                    "-Wl,-Ttext=0x20000",
                    str(work / "fixture.c"),
                    *objects,
                    str(support),
                    "-lc",
                    "-lgcc",
                    "-o",
                    str(binary),
                ],
            )
            run(
                work,
                binary.name + "-run",
                ["qemu-arm", "-cpu", "cortex-m0", str(binary)],
                marker=marker,
            )
            run(work, binary.name + "-size", ["arm-none-eabi-size", *objects])
        print("PASS ARMv6-M QEMU: partition and forced heap", flush=True)
    versions = {}
    for tool in ["cc", "clang", *(["arm-none-eabi-gcc", "qemu-arm"] if args.arm else [])]:
        versions[tool] = run(work, tool + "-version", [tool, "--version"])
    receipt = {
        "evidence_class": "host-execution-and-operation-counts",
        "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "fixture_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
        "counts_sha256": hashlib.sha256(counts.encode()).hexdigest(),
        "baseline_commit": BASELINE,
        "baseline_sha256": hashlib.sha256(baseline.encode()).hexdigest(),
        "versions": versions,
        "arm_execution": "pass" if args.arm else "not run: --arm unselected",
        "scope": "Experimental; target cycles and full resource admission remain open.",
    }
    (work / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("PASS bidirectional sort replay: " + str(work), flush=True)


if __name__ == "__main__":
    main()
