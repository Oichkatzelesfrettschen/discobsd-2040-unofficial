"""Measure an experimental all-global PIC link and a missing-root control."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def run(root, replay, output):
    sys.path.insert(0, str(root / "tools"))
    from libc_shared_closure import command, read_archive

    require(output.is_relative_to(root / ".build-logs") and not output.exists(),
            "link output requires a fresh evidence directory")
    receipt = json.loads((replay / "receipt.json").read_text())
    archive = replay / "libc-pic.a"
    require(digest(archive) == receipt["pic_archive_sha256"], "PIC archive identity differs")
    graph = read_archive(archive, "arm-none-eabi-ar")
    require({r["member"]: r["pic_sha256"] for r in receipt["members"]} ==
            {name: row["sha256"] for name, row in graph["members"].items()},
            "PIC replay member identities differ")
    roots = sorted(graph["providers"])
    require("memmem" in roots, "the omission control requires the memmem root")
    output.mkdir(parents=True)
    template = root / "tests/libc_xip_object_abi/library.ld"
    script = template.read_text().replace("ENTRY(shared_errno_address)", "ENTRY(exit)")
    script = script.replace("absolute_zero = 0;\n", "")
    script = script.replace(".got.plt :", "__libc_got_size = SIZEOF(.got);\n    .got.plt :")
    require("ENTRY(exit)" in script and script.count("__libc_got_size =") == 1,
            "linker template adaptation differs")
    (output / "library.ld").write_text(script)
    source = output / "startup.c"
    source.write_text('char **environ;\nconst char *__progname = "";\n')
    flags = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os", "-std=c17",
             "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-common", "-fPIC",
             "-msingle-pic-base", "-mpic-register=r9", "-mno-pic-data-is-text-relative"]
    compiler = "arm-none-eabi-gcc"
    require(not command([compiler, *flags, "-c", source, "-o", output / "startup.o"]),
            "startup fixture emits output")
    libgcc = Path(command([compiler, *flags, "-print-libgcc-file-name"]).decode().strip())
    report = {"schema_version": 1, "source_commit": receipt["source_commit"],
              "replay_receipt_sha256": digest(replay / "receipt.json"),
              "collector_sha256": digest(Path(__file__)), "root_count": len(roots),
              "roots": roots, "members": len(graph["members"]), "reservation_bytes": 49152,
              "libgcc_sha256": digest(libgcc), "linker_template_sha256": digest(template),
              "linker_script_sha256": digest(output / "library.ld"),
              "startup_source_sha256": digest(source), "links": {}}
    for mode, selected in (("all-global", roots),
                           ("omitted-memmem", [name for name in roots if name != "memmem"])):
        target = output / (mode + ".elf")
        argv = ["arm-none-eabi-ld", "--fatal-warnings", "-z", "noexecstack",
                "--emit-relocs", "--gc-sections", "-T", str(output / "library.ld"),
                "-Map=" + str(output / (mode + ".map")), "-o", str(target),
                "--whole-archive", str(archive), "--no-whole-archive", str(libgcc),
                str(output / "startup.o"), *("--undefined=" + name for name in selected)]
        result = subprocess.run(argv, capture_output=True, text=True, check=False,
                                timeout=60, env=dict(os.environ, LC_ALL="C"))
        (output / (mode + ".log")).write_text(result.stdout + result.stderr)
        (output / (mode + "-command.json")).write_text(json.dumps(argv, indent=2) + "\n")
        require(result.returncode == 0 and not result.stdout and not result.stderr,
                "experimental link fails or emits diagnostics")
        with target.open("rb") as stream:
            elf = ELFFile(stream)
            table = elf.get_section_by_name(".symtab")
            defined = {s.name: s for s in table.iter_symbols() if s["st_shndx"] != "SHN_UNDEF"}
            missing = sorted(set(roots) - set(defined))
            undefined = sorted(s.name for s in table.iter_symbols()
                               if s.name and s["st_shndx"] == "SHN_UNDEF")
            require(not undefined, "experimental image has unresolved imports")
            require(missing == ([] if mode == "all-global" else ["memmem"]),
                    "required-root retention control differs")
            segments = [{"address": p["p_paddr"], "file_bytes": p["p_filesz"],
                         "memory_bytes": p["p_memsz"], "flags": p["p_flags"]}
                        for p in elf.iter_segments() if p["p_type"] == "PT_LOAD"]
            require(len(segments) == 2 and {p["flags"] for p in segments} == {5, 6},
                    "experimental segment layout differs")
            flash = max(p["address"] + p["file_bytes"] for p in segments) - 0x10020000
            report["links"][mode] = {
                "elf_sha256": digest(target), "flash_bytes": flash,
                "state_bytes": defined["__libc_state_size"]["st_value"],
                "initialization_bytes": defined["__libc_init_end"]["st_value"],
                "missing_required_roots": missing, "undefined_symbols": undefined,
                "segments": segments, "allocated_sections":
                {s.name: s["sh_size"] for s in elf.iter_sections() if s["sh_flags"] & 2}}
    require(digest(archive) == receipt["pic_archive_sha256"], "PIC archive changes during link")
    report["claim"] = ("All-global experimental PIC layout before stable slots, public ABI "
                       "selection, startup relocation or execution; the omission image is invalid.")
    (output / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = run(args.root.resolve(), args.replay.resolve(), args.output.resolve())
    row = report["links"]["all-global"]
    print(f"All-global experimental layout: {row['flash_bytes']} flash, {row['state_bytes']} state")


if __name__ == "__main__":
    main()
