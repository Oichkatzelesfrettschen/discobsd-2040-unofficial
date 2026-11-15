"""Calibrate shared callable identity with real ARM objects and instructions."""

import argparse
import hashlib
import io
import json
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
        result = subprocess.run(argv, cwd=self.work, capture_output=True, timeout=90, check=False)
        index = len(self.commands)
        self.commands.append({"argv": argv, "exit": result.returncode})
        (self.work / f"command-{index}.stdout").write_bytes(result.stdout)
        (self.work / f"command-{index}.stderr").write_bytes(result.stderr)
        if rejection is not None:
            require(
                result.returncode != 0 and rejection.encode() in result.stderr,
                "expected link rejection differs: " + rejection,
            )
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
            edits = separate(provider, split, PUBLIC)
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
            self.command([self.cc, *FLAGS, "-c", HERE / "provider.S", "-o", target])
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
            rejection="multiple definition"
            if fault in ("omitted-separation", "interposition")
            else None,
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
    reject(lambda: separate(path, work / (field + "-separated.o"), PUBLIC), expected)


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
                    reject(
                        lambda provider=provider: separate(
                            provider, work / "missing.o", ("api_missing",)
                        ),
                        "implementation alias is absent: api_missing",
                    )
                    receipt["controls"].append("missing implementation alias")
                    reject(
                        lambda image=image: separate(image, work / "executable.o", PUBLIC),
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
                        lambda raw=raw: separate(raw, work / "raw-lto.o", PUBLIC),
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
