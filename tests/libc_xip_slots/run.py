"""Calibrate shared callable identity with real ARM objects and instructions."""

import argparse
import hashlib
import io
import json
import os
import struct
import subprocess
import uuid
from pathlib import Path
from typing import TypedDict

import elftools
import unicorn
from elftools.elf.elffile import ELFFile
from runtime import exercise
from symbols import layout, require, separate

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GROUPS = (("api_sum", "api_alias"), ("api_xor",))
PUBLIC = tuple(name for group in GROUPS for name in group)
FLAGS = [
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-mfloat-abi=soft",
    "-std=c17",
    "-pedantic-errors",
    "-Os",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-fno-builtin",
    "-ffreestanding",
    "-fno-common",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
]
PIC = ["-fPIC", "-msingle-pic-base", "-mno-pic-data-is-text-relative"]


def require_collision(returncode, diagnostic, symbol):
    require(
        returncode != 0 and f"multiple definition of `{symbol}'".encode() in diagnostic,
        "expected link rejection differs: " + symbol,
    )


class Case(TypedDict):
    name: str
    calls: int
    elf_sha256: str
    application_sha256: str
    state_bytes: int
    flash_bytes: int
    text_image_bytes: int
    initialized_image_bytes: int


class Receipt(TypedDict, total=False):
    status: str
    commands: list[dict[str, object]]
    cases: list[Case]
    controls: list[str]
    source_head: str
    sources: dict[str, str]
    compiler: str
    pyelftools: str
    unicorn: str


class Runner:
    def __init__(self, work, cc, objcopy):
        self.work = work
        self.cc = cc
        self.objcopy = objcopy
        self.commands: list[dict[str, object]] = []

    def command(self, argv, rejection=None, output=False):
        argv = [str(value) for value in argv]
        result = subprocess.run(
            argv,
            cwd=self.work,
            capture_output=True,
            timeout=90,
            check=False,
            env={**os.environ, "LC_ALL": "C"},
        )
        index = len(self.commands)
        self.commands.append({"argv": argv, "exit": result.returncode})
        (self.work / f"command-{index}.stdout").write_bytes(result.stdout)
        (self.work / f"command-{index}.stderr").write_bytes(result.stderr)
        if rejection is not None:
            require_collision(result.returncode, result.stderr, rejection)
        else:
            require(
                result.returncode == 0 and not result.stderr and (output or not result.stdout),
                f"command {index} fails or emits diagnostics: "
                + result.stderr.decode(errors="replace"),
            )
        return result.stdout.decode()

    def build(self, name, *, lto=False, interposition=True, fault=None):
        work = self.work / name
        work.mkdir()
        flags = FLAGS + PIC + ([] if interposition else ["-fno-semantic-interposition"])
        compile_flags = flags + (["-flto", "-ffat-lto-objects"] if lto else [])
        definitions = {
            "returned-pointer": "-DRETURNED_BYPASS=1",
            "initialized-pointer": "-DINITIALIZED_BYPASS=1",
        }
        objects = []
        for stem in ("provider.c", "consumer.c", "provider.S"):
            target = work / (stem + ".o")
            self.command(
                [
                    self.cc,
                    *compile_flags,
                    *([definitions[fault]] if fault in definitions else []),
                    "-c",
                    HERE / stem,
                    "-o",
                    target,
                ]
            )
            objects.append(target)
        provider = work / "combined.o"
        self.command(
            [
                self.cc,
                *flags,
                *(["-flto", "-flinker-output=nolto-rel"] if lto else []),
                "-nostdlib",
                "-r",
                *objects,
                "-o",
                provider,
            ]
        )
        split = work / "separated.o"
        if fault not in ("omitted-separation", "assignment"):
            edits = separate(provider, split, GROUPS)
            (work / "separation.json").write_text(
                json.dumps(
                    {
                        "input_sha256": hashlib.sha256(provider.read_bytes()).hexdigest(),
                        "output_sha256": hashlib.sha256(split.read_bytes()).hexdigest(),
                        "symbols": edits,
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            )
        else:
            split = provider
        groups = (("api_sum",), ("api_alias",), ("api_xor",)) if fault == "split-alias" else GROUPS
        prefix = "__slot_" if fault == "assignment" else ""
        assembly = [
            ".syntax unified",
            ".cpu cortex-m0plus",
            ".thumb",
            '.section .text.slots,"ax",%progbits',
            ".balign 4",
        ]
        for group in groups:
            name = prefix + group[0]
            target = "__libc_impl_" + ("api_xor" if fault == "wrong-target" else group[0])
            size = 8 if fault == "extent" else 12
            pop = "pop {r2, pc}" if fault == "argument-register" else "pop {r3, pc}"
            assembly += [
                f".global {name}",
                f".type {name},%function",
                ".thumb_func",
                name + ":",
                "push {r3, lr}",
                "ldr r3, 1f",
                "str r3, [sp, #4]",
                pop,
                "1: .word " + target,
                f".size {name},{size}",
            ]
            for alias in group[1:]:
                alias = prefix + alias
                assembly += [
                    f".global {alias}",
                    f".set {alias},{name}",
                    f".type {alias},%function",
                    f".size {alias},12",
                ]
        (work / "slots.S").write_text("\n".join(assembly) + "\n")
        self.command([self.cc, *flags, "-c", work / "slots.S", "-o", work / "slots.o"])
        script = (HERE / "library.ld").read_text()
        if fault == "assignment":
            script += "\n".join(f"{name} = __slot_{name};" for name in PUBLIC) + "\n"
        (work / "library.ld").write_text(script)
        inputs = [split, work / "slots.o"]
        if fault == "interposition":
            target = work / "interposition.o"
            source = work / "public-only.S"
            source.write_text(
                ".syntax unified\n.cpu cortex-m0plus\n.thumb\n.text\n"
                ".global api_xor\n.type api_xor,%function\n"
                ".thumb_func\napi_xor:\n bx lr\n.size api_xor,.-api_xor\n"
            )
            self.command([self.cc, *FLAGS, "-c", source, "-o", target])
            inputs.append(target)
        image = work / "library.elf"
        self.command(
            [
                self.cc,
                *flags,
                "-nostdlib",
                "-Xlinker",
                "--fatal-warnings",
                "-Xlinker",
                "--emit-relocs",
                "-Xlinker",
                "-T",
                "-Xlinker",
                work / "library.ld",
                *inputs,
                "-o",
                image,
            ],
            rejection={"omitted-separation": "api_sum", "interposition": "api_xor"}.get(fault),
        )
        return image, provider

    def application(self, work):
        obj = work / "application.o"
        self.command(
            [
                self.cc,
                *FLAGS,
                "-fno-pic",
                "-ffixed-r9",
                "-Dpeer_saved=app_saved",
                "-Dpeer_address=app_address",
                "-Dpeer_call=app_call",
                "-c",
                HERE / "consumer.c",
                "-o",
                obj,
            ]
        )
        imports = work / "imports.elf"
        self.command(
            [
                self.objcopy,
                "--strip-all",
                *(f"--keep-symbol={name}" for name in PUBLIC),
                work / "library.elf",
                imports,
            ]
        )
        script = """
ENTRY(app_call)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x20010000;
  .text : { *(.text .text.*) } :text
  .data : { *(.data .data.*) } :data
  .bss (NOLOAD) : { *(.bss .bss.*) } :data
  /DISCARD/ : { *(.ARM.exidx* .ARM.extab*) }
}
"""
        (work / "application.ld").write_text(script)
        path = work / "application.elf"
        self.command(
            [
                self.cc,
                *FLAGS,
                "-nostdlib",
                "-Xlinker",
                "--fatal-warnings",
                "-Xlinker",
                "-T",
                "-Xlinker",
                work / "application.ld",
                "-Xlinker",
                "--just-symbols",
                "-Xlinker",
                imports,
                obj,
                "-o",
                path,
            ]
        )
        return path


def reject(action, expected):
    try:
        action()
    except ValueError as error:
        require(str(error) == expected, "rejection reason differs: " + str(error))
    else:
        raise ValueError("control accepts malformed input: " + expected)


def symbol_control(provider, work, field, change, expected):
    data = bytearray(provider.read_bytes())
    elf = ELFFile(io.BytesIO(data))
    table = elf.get_section_by_name(".symtab")
    symbol_name = "__libc_impl_api_sum" if field == "owner" else "api_sum"
    found = 0
    for index, symbol in enumerate(table.iter_symbols()):
        if symbol.name != symbol_name:
            continue
        offset = table["sh_offset"] + index * 16
        fields = list(struct.unpack_from("<IIIBBH", data, offset))
        change(fields)
        struct.pack_into("<IIIBBH", data, offset, *fields)
        found += 1
    require(found == 1, "symbol control construction differs")
    path = work / (field + ".o")
    with path.open("xb") as stream:
        stream.write(data)
    reject(lambda: separate(path, work / (field + "-separated.o"), GROUPS), expected)


def review_controls(runner, provider, image, application):
    work = provider.parent
    data = bytearray(provider.read_bytes())
    elf = ELFFile(io.BytesIO(data))
    table = elf.get_section_by_name(".symtab")
    other = table.get_symbol_by_name("api_xor")[0]
    changed = 0
    for index, symbol in enumerate(table.iter_symbols()):
        if symbol.name in ("api_alias", "__libc_impl_api_alias"):
            offset = table["sh_offset"] + index * 16
            fields = list(struct.unpack_from("<IIIBBH", data, offset))
            fields[1], fields[2], fields[5] = (
                other["st_value"],
                other["st_size"],
                other["st_shndx"],
            )
            struct.pack_into("<IIIBBH", data, offset, *fields)
            changed += 1
    require(changed == 2, "alias-group control construction differs")
    bad = work / "conflicting-group.o"
    bad.write_bytes(data)
    reject(
        lambda: separate(bad, work / "conflicting-group-separated.o", GROUPS),
        "alias group implementation differs: api_alias",
    )

    source = work / "private-only.S"
    source.write_text(
        ".syntax unified\n.cpu cortex-m0plus\n.thumb\n.text\n"
        ".global __libc_impl_api_xor\n.type __libc_impl_api_xor,%function\n"
        ".thumb_func\n__libc_impl_api_xor:\n bx lr\n"
    )
    runner.command([runner.cc, *FLAGS, "-c", source, "-o", work / "private-only.o"])
    runner.command(
        [
            runner.cc,
            *FLAGS,
            *PIC,
            "-nostdlib",
            "-Xlinker",
            "--fatal-warnings",
            "-Xlinker",
            "-T",
            "-Xlinker",
            work / "library.ld",
            work / "separated.o",
            work / "slots.o",
            work / "private-only.o",
            "-o",
            work / "private-collision.elf",
        ],
        rejection="__libc_impl_api_xor",
    )
    index = len(runner.commands) - 1
    diagnostic = (runner.work / f"command-{index}.stderr").read_bytes()
    reject(
        lambda: require_collision(runner.commands[index]["exit"], diagnostic, "api_xor"),
        "expected link rejection differs: api_xor",
    )

    data = bytearray(application.read_bytes())
    elf = ELFFile(io.BytesIO(data))
    table = elf.get_section_by_name(".symtab")
    value = table.get_symbol_by_name("api_sum")[0]["st_value"]
    changed = 0
    for index, symbol in enumerate(table.iter_symbols()):
        if symbol.name == "api_xor":
            struct.pack_into("<I", data, table["sh_offset"] + index * 16 + 4, value)
            changed += 1
    require(changed == 1, "application import control construction differs")
    bad = work / "incorrect-import.elf"
    bad.write_bytes(data)
    reject(lambda: exercise(image, bad), "application import address differs: api_xor")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="arm-none-eabi-gcc")
    parser.add_argument("--objcopy", default="arm-none-eabi-objcopy")
    args = parser.parse_args()
    work = ROOT / ".build-logs" / ("xip-callable-identity-" + uuid.uuid4().hex)
    work.mkdir(parents=True)
    runner = Runner(work, args.cc, args.objcopy)
    receipt: Receipt = {
        "status": "failed",
        "commands": runner.commands,
        "cases": [],
        "controls": [],
    }
    try:
        require(
            tuple(int(part) for part in unicorn.__version__.split(".")[:1]) >= (2,),
            "Unicorn 2 or later is required",
        )
        receipt.update(
            {
                "source_head": runner.command(
                    ["git", "-C", ROOT, "rev-parse", "HEAD"], output=True
                ).strip(),
                "sources": {
                    str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted(HERE.iterdir())
                    if path.is_file()
                },
                "compiler": runner.command([args.cc, "--version"], output=True).splitlines()[0],
                "pyelftools": elftools.__version__,
                "unicorn": unicorn.__version__,
            }
        )
        for lto in (False, True):
            for interposition in (True, False):
                name = ("lto" if lto else "native") + (
                    "-public" if interposition else "-local-calls"
                )
                image, provider = runner.build(name, lto=lto, interposition=interposition)
                inspected = layout(image, GROUPS)
                application = runner.application(image.parent)
                calls = exercise(inspected, application)
                receipt["cases"].append(
                    {
                        "name": name,
                        "calls": calls,
                        "elf_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                        "application_sha256": hashlib.sha256(application.read_bytes()).hexdigest(),
                        "state_bytes": inspected["state_size"],
                        "flash_bytes": inspected["flash_size"],
                        "text_image_bytes": inspected["text_image_size"],
                        "initialized_image_bytes": inspected["initialized_size"],
                    }
                )
                print(f"{name}: {calls} calls PASS", flush=True)
                if name == "native-public":
                    review_controls(runner, provider, inspected, application)
                    receipt["controls"].extend(
                        (
                            "conflicting alias-group owner",
                            "unrelated private collision",
                            "incorrect application import address",
                        )
                    )
                    reject(
                        lambda provider=provider: separate(
                            provider, work / "missing.o", (("api_missing",),)
                        ),
                        "implementation alias is absent: api_missing",
                    )
                    receipt["controls"].append("missing implementation alias")
                    reject(
                        lambda image=image: separate(image, work / "executable.o", GROUPS),
                        "separation requires little-endian ARM ELF32 relocatable input",
                    )
                    receipt["controls"].append("executable separation input")
                    symbol_control(
                        provider,
                        work,
                        "owner",
                        lambda fields: fields.__setitem__(1, fields[1] + 2),
                        "implementation alias owner differs: api_sum",
                    )
                    receipt["controls"].append("conflicting implementation owner")
                    symbol_control(
                        provider,
                        work,
                        "type",
                        lambda fields: fields.__setitem__(3, 0x10),
                        "provider type, binding or visibility differs: api_sum",
                    )
                    receipt["controls"].append("non-function public provider")
                if name == "lto-public":
                    raw = image.parent / "provider.c.o"
                    reject(
                        lambda raw=raw: separate(raw, work / "raw-lto.o", GROUPS),
                        "LTO input requires native lowering before separation",
                    )
                    receipt["controls"].append("unlowered LTO input")
        for fault in ("omitted-separation", "interposition"):
            runner.build(fault, fault=fault)
            receipt["controls"].append(fault)
        for fault, expected in (
            ("assignment", "function extent differs: api_sum"),
            ("extent", "function extent differs: api_sum"),
            ("split-alias", "canonical alias address differs: api_alias"),
            ("returned-pointer", "returned public pointer differs"),
            ("initialized-pointer", "initialized public pointer differs"),
            ("argument-register", "call result differs"),
            ("wrong-target", "call result differs"),
        ):
            image, _ = runner.build(fault, fault=fault)

            def control(image=image):
                inspected = layout(image, GROUPS)
                application = runner.application(image.parent)
                exercise(inspected, application)

            reject(control, expected)
            receipt["controls"].append(fault)
        receipt["status"] = "passed"
        print(
            f"XIP callable identity PASS: {len(receipt['cases'])} images, "
            f"{sum(case['calls'] for case in receipt['cases'])} calls, "
            f"{len(receipt['controls'])} rejected controls; {work.relative_to(ROOT)}"
        )
    finally:
        (work / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
