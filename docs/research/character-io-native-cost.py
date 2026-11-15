"""Measure selected native character members against retained main identities."""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def require(value, message):
    if not value:
        raise ValueError(message)


def run(argv, cwd=None):
    result = subprocess.run([str(x) for x in argv], cwd=cwd, capture_output=True,
                            check=False, timeout=90)
    require(result.returncode == 0, result.stderr.decode())
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(os.environ.get("PYTHON"), "PYTHON must select the interpreter")
    root = args.root.resolve()
    sys.path.insert(0, str(root / "tools"))
    from check_multicall_composition import measure_hsaout
    base = json.loads(args.baseline.read_text())
    candidate = json.loads(args.candidate.read_text())
    require(base["toolchain"] == candidate["toolchain"], "toolchain differs")
    archive = root / "distrib/obj/boardlibc.rp2040/libc.a"
    require(hashlib.sha256(archive.read_bytes()).hexdigest() ==
            candidate["native_artifacts"]["/usr/lib/libc.a"]["sha256"], "archive differs")
    startup = root / "lib/libc_aout/crt0.o"
    startup_hash = hashlib.sha256(startup.read_bytes()).hexdigest()
    require(startup_hash == base["native_artifacts"]["/usr/lib/crt0.o"]["sha256"] ==
            candidate["native_artifacts"]["/usr/lib/crt0.o"]["sha256"], "startup differs")
    require(run(["arm-none-eabi-gcc", "--version"]).decode().splitlines()[0] ==
            candidate["toolchain"]["cc"], "current compiler differs")
    tools = root / "tools/bin/rp2040"
    tool_hashes = {name: hashlib.sha256((tools / name).read_bytes()).hexdigest()
                   for name in ("as", "ar", "ranlib", "ld", "hsaout")}
    require(tool_hashes["hsaout"] == candidate["measurement_tools"]["hsaout"],
            "measurement tool differs")
    flags = ["-std=gnu17", "-mcpu=cortex-m0plus", "-mabi=aapcs", "-mlittle-endian",
             "-mthumb", "-mfloat-abi=soft", "-nostdinc", "-I" + str(root / "include"),
             "-Os", "-fno-common", "-Wall", "-Wextra", "-Werror", "-Wformat=2",
             "-ffreestanding", "-fno-jump-tables", "-D_TIME_BITS=32"]
    old_members = base["archive_selections"]["native.usr.lib.libc.a"][
        "tree:distrib/obj/boardlibc.rp2040/libc.a"]
    cases = []
    with tempfile.TemporaryDirectory(prefix="character-native-cost-") as temporary:
        work = Path(temporary)
        old = work / "baseline.a"
        shutil.copyfile(archive, old)
        run([tools / "ar", "d", old, "getchar.o", "putchar.o"])
        for name in ("fgetc", "fputc"):
            source = work / (name + ".c")
            source.write_bytes(run(["git", "-C", root, "show",
                                   base["source"]["commit"] + ":lib/libc/stdio/" + name + ".c"]))
            assembly, obj = source.with_suffix(".s"), source.with_suffix(".o")
            run(["arm-none-eabi-gcc", *flags, "-S", source, "-o", assembly])
            run([tools / "as", "-X", assembly, "-o", obj])
            require(hashlib.sha256(obj.read_bytes()).hexdigest() == old_members[name + ".o"],
                    "replayed baseline member differs: " + name)
            run([tools / "ar", "r", old, obj])
        run([tools / "ranlib", old])
        for label, declarations in (
            ("input", "int (*volatile input)(FILE *) = fgetc;"),
            ("output", "int (*volatile output)(int, FILE *) = fputc;"),
            ("both", "int (*volatile input)(FILE *) = fgetc;\n"
                     "int (*volatile output)(int, FILE *) = fputc;")):
            source = work / (label + ".c")
            source.write_text("#include <stdio.h>\n" + declarations +
                              "\nint main(void) { return 0; }\n")
            assembly, obj = source.with_suffix(".s"), source.with_suffix(".o")
            run(["arm-none-eabi-gcc", *flags, "-S", source, "-o", assembly])
            run([tools / "as", "-X", assembly, "-o", obj])
            values = {}
            for side, library in (("baseline", old), ("candidate", archive)):
                image = work / (label + "-" + side)
                trace = run([tools / "ld", "-t", "-o", image,
                             root / "lib/libc_aout/crt0.o", obj, library]).decode()
                selected = sorted(set(re.findall(r"\(([^()]*)\):", trace)))
                words = struct.unpack("<8I", image.read_bytes()[:32])
                raw, packed, raw_blocks, packed_blocks = measure_hsaout(
                    root, tools / "hsaout", [{"path": str(image)}])[0]
                values[side] = dict(text=words[1], data=words[2], bss=words[3],
                    loaded=sum(words[1:4]), raw=raw, packed=packed,
                    raw_blocks=raw_blocks, packed_blocks=packed_blocks,
                    sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                    selected_members=selected)
            delta = {key: values["candidate"][key] - values["baseline"][key]
                     for key in values["baseline"] if key not in {"sha256", "selected_members"}}
            cases.append(dict(case=label, values=values, deltas=delta))
    require(hashlib.sha256(archive.read_bytes()).hexdigest() ==
            candidate["native_artifacts"]["/usr/lib/libc.a"]["sha256"],
            "archive changes during measurement")
    receipt = dict(schema_version=1, tools=tool_hashes, startup_sha256=startup_hash,
                   verifier_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   baseline=base["source"], candidate=candidate["source"],
                   baseline_member_replay="two exact retained SHA-256 matches",
                   cases=cases, runtime="unrun", physical_hardware="unrun")
    args.output.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({row["case"]: row["deltas"] for row in cases}, indent=2))


if __name__ == "__main__":
    main()
