"""Verify the RP2040 ELF load layout and elf2aout conversion contract."""

from __future__ import annotations

import argparse
import dataclasses
import itertools
import pathlib
import struct
import subprocess
import tempfile

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIIIIIII")
SECTION_HEADER = struct.Struct("<IIIIIIIIII")
AOUT_HEADER = struct.Struct("<8I")

PT_LOAD = 1
PT_GNU_STACK = 0x6474E551
PF_X = 1
PF_W = 2
PF_R = 4
SHF_WRITE = 1
SHF_ALLOC = 2
SHF_EXECINSTR = 4
SHT_NOBITS = 8
OMAGIC = 0o407
N_BSS = 0x04
N_EXT = 0x20


ASSEMBLY_SOURCE = r"""
	.syntax unified
	.thumb

#if INCLUDE_TEXT
	.section .text.startup,"ax",%progbits
	.global _start
	.type _start,%function
	.thumb_func
_start:
	bx	lr
#endif

#if INCLUDE_RODATA
	.section .rodata.fixture,"a",%progbits
	.byte	0x10, 0x20, 0x30
#endif

#if INCLUDE_OPTIONAL_DATA
	.section .preinit_array,"aw",%preinit_array
	.word	0x10203040
	.section .init_array,"aw",%init_array
	.word	0x50607080
	.section .fini_array,"aw",%fini_array
	.word	0x90a0b0c0
	.section .data.rel.ro.fixture,"aw",%progbits
	.word	0xd0e0f000
	.section .got.fixture,"aw",%progbits
	.word	0x01020304
#endif

#if INCLUDE_DATA
	.section .data.fixture,"aw",%progbits
#if !INCLUDE_TEXT
	.global _start
_start:
#endif
	.byte	0x11, 0x22, 0x33, 0x44, 0x55
#endif

#if INCLUDE_BSS
	.section .bss.fixture,"aw",%nobits
#if !INCLUDE_TEXT && !INCLUDE_DATA
	.global _start
_start:
#endif
	.space	13
#endif
"""


RWX_LINKER_SCRIPT = r"""
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  . = 0x20000000;
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(COMMON) }
}
"""


OVERLAY_DISPATCHER_SOURCE = r"""
	.syntax unified
	.thumb
	.section .text.startup,"ax",%progbits
	.global _start
	.type _start,%function
	.thumb_func
_start:
	bx	lr

	.section .bss.shared,"aw",%nobits
	.balign 4
	.global shared_canary
shared_canary:
	.space 20
"""


OVERLAY_ALPHA_SOURCE = r"""
	.syntax unified
	.thumb
	.section .text.alpha,"ax",%progbits
	.global alpha_main
	.type alpha_main,%function
	.thumb_func
alpha_main:
	ldr	r0, =alpha_canary
	bx	lr

	.section .data.alpha,"aw",%progbits
	.global alpha_seed
alpha_seed:
	.word 0x11223344

	.section .bss,"aw",%nobits
	.balign 4
	.global alpha_canary
alpha_canary:
	.space 64
"""


OVERLAY_BETA_SOURCE = r"""
	.syntax unified
	.thumb
	.section .text.beta,"ax",%progbits
	.global beta_main
	.type beta_main,%function
	.thumb_func
beta_main:
	ldr	r0, =beta_canary
	bx	lr

	.section .data.beta,"aw",%progbits
	.global beta_seed
beta_seed:
	.word 0x55667788

	.section .bss,"aw",%nobits
	.balign 8
	.global beta_canary
beta_canary:
	.space 112
"""


OVERLAY_CROSS_REFERENCE_SOURCE = r"""
	.syntax unified
	.thumb
	.section .bss,"aw",%nobits
	.balign 4
	.global alpha_canary
alpha_canary:
	.reloc alpha_canary, R_ARM_ABS32, beta_canary
	.space 64
"""


OVERLAY_COMMON_SOURCE = r"""
	.syntax unified
	.thumb
	.section .text.common,"ax",%progbits
	.global common_main
	.type common_main,%function
	.thumb_func
common_main:
	bx	lr

	.comm common_canary,64,4
"""


OVERLAY_ESCAPED_NOBITS_SOURCE = r"""
	.syntax unified
	.thumb
	.section .text.escaped,"ax",%progbits
	.global escaped_main
	.type escaped_main,%function
	.thumb_func
escaped_main:
	bx	lr

	.section .bss,"aw",%nobits
	.global expected_canary
expected_canary:
	.space 4

	.section .escaped_bss,"aw",%nobits
	.global escaped_canary
escaped_canary:
	.space 64
"""


NO_PHDR_LINKER_SCRIPT = r"""
ENTRY(_start)
SECTIONS
{
  . = 0x20000000;
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(COMMON) }
}
"""


MIPS_OVERLAY_DISPATCHER_SOURCE = r"""
void
_start(void)
{
}

unsigned char shared_canary[20];
"""


MIPS_OVERLAY_ALPHA_SOURCE = r"""
unsigned int alpha_seed = 0x11223344;
unsigned char alpha_canary[64];
unsigned int alpha_small_canary;

void
alpha_main(void)
{
	alpha_canary[0] = (unsigned char)alpha_seed;
	alpha_small_canary = alpha_seed;
}
"""


MIPS_OVERLAY_BETA_SOURCE = r"""
unsigned int beta_seed = 0x55667788;
unsigned char beta_canary[112] __attribute__((aligned(8)));

void
beta_main(void)
{
	beta_canary[0] = (unsigned char)beta_seed;
}
"""


@dataclasses.dataclass(frozen=True)
class LayoutVariant:
    name: str
    include_text: bool
    include_rodata: bool
    include_optional_data: bool
    include_data: bool
    include_bss: bool


@dataclasses.dataclass(frozen=True)
class ProgramHeader:
    index: int
    header_type: int
    offset: int
    virtual_address: int
    file_size: int
    memory_size: int
    flags: int
    alignment: int


@dataclasses.dataclass(frozen=True)
class SectionHeader:
    name: str
    section_type: int
    flags: int
    virtual_address: int
    offset: int
    size: int


@dataclasses.dataclass(frozen=True)
class ElfImage:
    content: bytes
    entry: int
    program_header_offset: int
    program_header_size: int
    program_headers: tuple[ProgramHeader, ...]
    section_headers: tuple[SectionHeader, ...]


LAYOUT_VARIANTS = (
    LayoutVariant("text_data_bss", True, True, False, True, True),
    LayoutVariant("text_only", True, True, False, False, False),
    LayoutVariant("text_bss", True, True, False, False, True),
    LayoutVariant("text_data", True, True, False, True, False),
    LayoutVariant("data_only", False, False, False, True, False),
    LayoutVariant("bss_only", False, False, False, False, True),
    LayoutVariant("optional_data", True, True, True, True, True),
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    parser.add_argument("--ld", required=True)
    parser.add_argument("--make", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--linker-script", type=pathlib.Path, required=True)
    parser.add_argument("--elf2aout", type=pathlib.Path, required=True)
    parser.add_argument("--overlay-generator", type=pathlib.Path, required=True)
    parser.add_argument("--overlay-verifier", type=pathlib.Path, required=True)
    parser.add_argument("--mips-cc", required=True)
    parser.add_argument("--mips-ld", required=True)
    parser.add_argument("--mips-nm", required=True)
    parser.add_argument("--mips-objcopy", required=True)
    parser.add_argument("--mips-readelf", required=True)
    parser.add_argument("--mips-linker-script", type=pathlib.Path, required=True)
    return parser.parse_args()


def run_command(
    command: list[str], *, expected_status: int = 0
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != expected_status:
        raise SystemExit(
            f"command status {result.returncode}, expected {expected_status}: "
            f"{' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def read_c_string(string_table: bytes, offset: int) -> str:
    if offset >= len(string_table):
        raise SystemExit(f"section name offset {offset} exceeds string table")
    string_end = string_table.find(b"\0", offset)
    if string_end < 0:
        raise SystemExit("unterminated section name")
    return string_table[offset:string_end].decode("ascii")


def parse_elf(elf_path: pathlib.Path) -> ElfImage:
    content = elf_path.read_bytes()
    if len(content) < ELF_HEADER.size:
        raise SystemExit(f"short ELF header: {elf_path}")
    header_fields = ELF_HEADER.unpack_from(content)
    identification = header_fields[0]
    if identification[:6] != b"\x7fELF\x01\x01":
        raise SystemExit(f"expected little-endian ELF32 input: {elf_path}")

    entry = header_fields[4]
    program_header_offset = header_fields[5]
    section_header_offset = header_fields[6]
    program_header_size = header_fields[9]
    program_header_count = header_fields[10]
    section_header_size = header_fields[11]
    section_header_count = header_fields[12]
    section_name_index = header_fields[13]
    if program_header_count > 0 and program_header_size != PROGRAM_HEADER.size:
        raise SystemExit(f"unexpected program-header size: {program_header_size}")
    if section_header_size != SECTION_HEADER.size:
        raise SystemExit(f"unexpected section-header size: {section_header_size}")
    if section_name_index >= section_header_count:
        raise SystemExit("section-name table index exceeds section count")

    program_headers = []
    for program_index in range(program_header_count):
        field_offset = program_header_offset + program_index * program_header_size
        fields = PROGRAM_HEADER.unpack_from(content, field_offset)
        program_headers.append(
            ProgramHeader(
                index=program_index,
                header_type=fields[0],
                offset=fields[1],
                virtual_address=fields[2],
                file_size=fields[4],
                memory_size=fields[5],
                flags=fields[6],
                alignment=fields[7],
            )
        )

    raw_section_headers = []
    for section_index in range(section_header_count):
        field_offset = section_header_offset + section_index * section_header_size
        raw_section_headers.append(SECTION_HEADER.unpack_from(content, field_offset))
    name_fields = raw_section_headers[section_name_index]
    name_table = content[name_fields[4] : name_fields[4] + name_fields[5]]
    section_headers = tuple(
        SectionHeader(
            name=read_c_string(name_table, fields[0]),
            section_type=fields[1],
            flags=fields[2],
            virtual_address=fields[3],
            offset=fields[4],
            size=fields[5],
        )
        for fields in raw_section_headers
    )
    return ElfImage(
        content=content,
        entry=entry,
        program_header_offset=program_header_offset,
        program_header_size=program_header_size,
        program_headers=tuple(program_headers),
        section_headers=section_headers,
    )


def require_clean_command(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.stdout or result.stderr:
        raise SystemExit(
            f"{label} emitted diagnostics\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def build_variant(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    assembly_path: pathlib.Path,
    variant: LayoutVariant,
) -> pathlib.Path:
    object_path = temporary_directory / f"{variant.name}.o"
    elf_path = temporary_directory / f"{variant.name}.elf"
    macro_values = {
        "INCLUDE_TEXT": variant.include_text,
        "INCLUDE_RODATA": variant.include_rodata,
        "INCLUDE_OPTIONAL_DATA": variant.include_optional_data,
        "INCLUDE_DATA": variant.include_data,
        "INCLUDE_BSS": variant.include_bss,
    }
    compiler_command = [
        arguments.cc,
        "-x",
        "assembler-with-cpp",
        "-mcpu=cortex-m0plus",
        "-mthumb",
        "-mfloat-abi=soft",
        "-Wa,--fatal-warnings",
    ]
    compiler_command.extend(
        f"-D{macro_name}={int(enabled)}" for macro_name, enabled in macro_values.items()
    )
    compiler_command.extend(["-c", str(assembly_path), "-o", str(object_path)])
    require_clean_command(run_command(compiler_command), f"{variant.name} compile")

    linker_result = run_command(
        [
            arguments.ld,
            "-n",
            "--warn-rwx-segments",
            "--fatal-warnings",
            "-T",
            str(arguments.linker_script),
            "-o",
            str(elf_path),
            str(object_path),
        ]
    )
    require_clean_command(linker_result, f"{variant.name} link")
    return elf_path


def loadable_segments(elf_image: ElfImage) -> list[ProgramHeader]:
    return sorted(
        (
            header
            for header in elf_image.program_headers
            if header.header_type == PT_LOAD and header.memory_size > 0
        ),
        key=lambda header: header.virtual_address,
    )


def verify_segment_permissions(elf_image: ElfImage, variant: LayoutVariant) -> None:
    segments = loadable_segments(elf_image)
    expected_segment_count = (
        2
        if variant.include_text
        and (
            variant.include_optional_data or variant.include_data or variant.include_bss
        )
        else 1
    )
    if len(segments) != expected_segment_count:
        raise SystemExit(
            f"{variant.name}: {len(segments)} non-empty LOADs, "
            f"expected {expected_segment_count}"
        )
    for segment in segments:
        if segment.flags not in (PF_R | PF_X, PF_R | PF_W):
            raise SystemExit(
                f"{variant.name}: LOAD {segment.index} flags {segment.flags:#x}"
            )
        if segment.alignment > 1 and (
            segment.virtual_address % segment.alignment
            != segment.offset % segment.alignment
        ):
            raise SystemExit(f"{variant.name}: incongruent LOAD alignment")
    for previous, current in itertools.pairwise(segments):
        if previous.virtual_address + previous.memory_size != current.virtual_address:
            raise SystemExit(f"{variant.name}: non-contiguous LOAD address space")

    for section in elf_image.section_headers:
        if not (section.flags & SHF_ALLOC) or section.size == 0:
            continue
        section_end = section.virtual_address + section.size
        owning_segments = [
            segment
            for segment in segments
            if section.virtual_address >= segment.virtual_address
            and section_end <= segment.virtual_address + segment.memory_size
        ]
        if len(owning_segments) != 1:
            raise SystemExit(f"{variant.name}: {section.name} lacks one owning LOAD")
        segment = owning_segments[0]
        if section.flags & SHF_WRITE and not segment.flags & PF_W:
            raise SystemExit(
                f"{variant.name}: writable {section.name} is not in RW LOAD"
            )
        if section.flags & SHF_EXECINSTR and not segment.flags & PF_X:
            raise SystemExit(
                f"{variant.name}: executable {section.name} is not in RX LOAD"
            )


def expected_aout_image(elf_image: ElfImage) -> tuple[tuple[int, ...], bytes]:
    segments = loadable_segments(elf_image)
    image_base = segments[0].virtual_address
    file_end = max(segment.virtual_address + segment.file_size for segment in segments)
    memory_end = max(
        segment.virtual_address + segment.memory_size for segment in segments
    )
    payload = bytearray(file_end - image_base)
    for segment in segments:
        payload_offset = segment.virtual_address - image_base
        segment_content = elf_image.content[
            segment.offset : segment.offset + segment.file_size
        ]
        if len(segment_content) != segment.file_size:
            raise SystemExit("LOAD extends beyond ELF input")
        payload[payload_offset : payload_offset + segment.file_size] = segment_content

    data_sections = [
        section
        for section in elf_image.section_headers
        if section.name == ".data"
        and section.flags & SHF_ALLOC
        and section.section_type != SHT_NOBITS
        and section.size > 0
    ]
    if data_sections:
        text_size = data_sections[0].virtual_address - image_base
    else:
        text_size = file_end - image_base
    data_size = file_end - image_base - text_size
    bss_size = memory_end - file_end
    header = (OMAGIC, text_size, data_size, bss_size, 0, 0, 0, elf_image.entry)
    return header, bytes(payload)


def verify_conversion(
    arguments: argparse.Namespace,
    elf_path: pathlib.Path,
    variant: LayoutVariant,
) -> bytes:
    elf_image = parse_elf(elf_path)
    verify_segment_permissions(elf_image, variant)
    aout_path = elf_path.with_suffix(".aout")
    require_clean_command(
        run_command([str(arguments.elf2aout), str(elf_path), str(aout_path)]),
        f"{variant.name} conversion",
    )
    aout_content = aout_path.read_bytes()
    expected_header, expected_payload = expected_aout_image(elf_image)
    if len(aout_content) < AOUT_HEADER.size:
        raise SystemExit(f"{variant.name}: short a.out output")
    actual_header = AOUT_HEADER.unpack_from(aout_content)
    if actual_header != expected_header:
        raise SystemExit(
            f"{variant.name}: a.out header {actual_header} != {expected_header}"
        )
    if aout_content[AOUT_HEADER.size :] != expected_payload:
        raise SystemExit(f"{variant.name}: a.out payload differs from ELF load image")
    return aout_content


def require_conversion_failure_preserves_output(
    arguments: argparse.Namespace,
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    expected_message: str,
) -> None:
    sentinel = b"existing output must survive rejected input\n"
    output_path.write_bytes(sentinel)
    result = run_command(
        [str(arguments.elf2aout), str(input_path), str(output_path)],
        expected_status=1,
    )
    if expected_message not in result.stderr:
        raise SystemExit(f"missing rejection diagnostic: {expected_message}")
    if output_path.read_bytes() != sentinel:
        raise SystemExit("rejected input changed the existing output")


def patch_program_header_word(
    source_path: pathlib.Path,
    destination_path: pathlib.Path,
    elf_image: ElfImage,
    program_index: int,
    word_index: int,
    value: int,
) -> None:
    patched_content = bytearray(elf_image.content)
    field_offset = (
        elf_image.program_header_offset
        + program_index * elf_image.program_header_size
        + word_index * 4
    )
    struct.pack_into("<I", patched_content, field_offset, value)
    destination_path.write_bytes(patched_content)


def verify_negative_controls(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    reference_elf_path: pathlib.Path,
    reference_object_path: pathlib.Path,
) -> None:
    rwx_script_path = temporary_directory / "rwx.ld"
    rwx_script_path.write_text(RWX_LINKER_SCRIPT, encoding="ascii")
    rwx_result = run_command(
        [
            arguments.ld,
            "-n",
            "--warn-rwx-segments",
            "--fatal-warnings",
            "-T",
            str(rwx_script_path),
            "-o",
            str(temporary_directory / "rwx.elf"),
            str(reference_object_path),
        ],
        expected_status=1,
    )
    if "LOAD segment with RWX permissions" not in rwx_result.stderr:
        raise SystemExit("RWX negative control missed the linker diagnostic")

    reference_image = parse_elf(reference_elf_path)
    nonempty_headers = loadable_segments(reference_image)
    malformed_size_path = temporary_directory / "filesz-larger-than-memsz.elf"
    malformed_header = nonempty_headers[0]
    patch_program_header_word(
        reference_elf_path,
        malformed_size_path,
        reference_image,
        malformed_header.index,
        4,
        malformed_header.memory_size + 1,
    )
    require_conversion_failure_preserves_output(
        arguments,
        malformed_size_path,
        temporary_directory / "malformed-output.aout",
        "file size larger than memory size",
    )

    unsupported_path = temporary_directory / "unsupported-header.elf"
    patch_program_header_word(
        reference_elf_path,
        unsupported_path,
        reference_image,
        nonempty_headers[-1].index,
        0,
        PT_GNU_STACK,
    )
    require_conversion_failure_preserves_output(
        arguments,
        unsupported_path,
        temporary_directory / "unsupported-output.aout",
        "type 6474e551 can't be converted",
    )


def verify_shorter_overwrite(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    elf_paths: dict[str, pathlib.Path],
    aout_images: dict[str, bytes],
) -> None:
    longer_image = aout_images["optional_data"]
    shorter_image = aout_images["text_only"]
    if len(longer_image) <= len(shorter_image):
        raise SystemExit("overwrite fixtures do not establish a stale-tail risk")
    overwrite_path = temporary_directory / "overwrite.aout"
    overwrite_path.write_bytes(longer_image)
    require_clean_command(
        run_command(
            [
                str(arguments.elf2aout),
                str(elf_paths["text_only"]),
                str(overwrite_path),
            ]
        ),
        "shorter overwrite",
    )
    if overwrite_path.read_bytes() != shorter_image:
        raise SystemExit("shorter conversion retained or changed output bytes")


def verify_symbol_conversion(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    elf_path: pathlib.Path,
) -> None:
    symbol_output_path = temporary_directory / "symbols.aout"
    require_clean_command(
        run_command(
            [
                str(arguments.elf2aout),
                "-s",
                str(elf_path),
                str(symbol_output_path),
            ]
        ),
        "symbol conversion",
    )
    content = symbol_output_path.read_bytes()
    header = AOUT_HEADER.unpack_from(content)
    symbol_bytes = header[6]
    if header[:6] != (OMAGIC, 0, 0, 0, 0, 0) or header[7] != 0:
        raise SystemExit(f"symbol conversion emitted unexpected header {header}")
    native_nlist_size = struct.calcsize("@PBBI")
    if symbol_bytes == 0 or symbol_bytes % native_nlist_size != 0:
        raise SystemExit("symbol table has an invalid native nlist extent")
    string_table_offset = AOUT_HEADER.size + symbol_bytes
    if len(content) < string_table_offset + 4:
        raise SystemExit("symbol conversion omitted its string-table length")
    string_bytes = struct.unpack_from("<I", content, string_table_offset)[0]
    if len(content) != string_table_offset + 4 + string_bytes:
        raise SystemExit("symbol string-table length does not match initialized bytes")
    string_table_end = len(content)
    for symbol_offset in range(
        AOUT_HEADER.size, string_table_offset, native_nlist_size
    ):
        string_index = struct.unpack_from("<I", content, symbol_offset)[0]
        absolute_name_offset = string_table_offset + string_index
        if string_index < 4 or absolute_name_offset >= string_table_end:
            raise SystemExit("symbol string-table index is outside initialized bytes")
        name_end = content.find(b"\0", absolute_name_offset, string_table_end)
        if name_end < 0 or content[absolute_name_offset] != ord("_"):
            raise SystemExit("symbol name is unterminated or lacks its ABI prefix")


def compile_overlay_object(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    object_name: str,
    source: str,
) -> pathlib.Path:
    source_path = temporary_directory / f"{object_name}.S"
    object_path = temporary_directory / f"{object_name}.o"
    source_path.write_text(source, encoding="ascii")
    require_clean_command(
        run_command(
            [
                arguments.cc,
                "-x",
                "assembler",
                "-mcpu=cortex-m0plus",
                "-mthumb",
                "-mfloat-abi=soft",
                "-Wa,--fatal-warnings",
                "-c",
                str(source_path),
                "-o",
                str(object_path),
            ]
        ),
        f"{object_name} overlay compile",
    )
    return object_path


def rename_overlay_bss(
    arguments: argparse.Namespace,
    source_path: pathlib.Path,
    applet_name: str,
    destination_directory: pathlib.Path,
) -> pathlib.Path:
    destination_path = destination_directory / f"{applet_name}.tool.o"
    require_clean_command(
        run_command(
            [
                arguments.objcopy,
                "--rename-section",
                f".bss=.app_bss_{applet_name}",
                str(source_path),
                str(destination_path),
            ]
        ),
        f"{applet_name} BSS rename",
    )
    return destination_path


def compile_mips_overlay_object(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    object_name: str,
    source: str,
) -> pathlib.Path:
    source_path = temporary_directory / f"{object_name}.c"
    object_path = temporary_directory / f"{object_name}.o"
    source_path.write_text(source, encoding="ascii")
    require_clean_command(
        run_command(
            [
                arguments.mips_cc,
                "-std=gnu17",
                "-mips32r2",
                "-EL",
                "-msoft-float",
                "-ffreestanding",
                "-fno-pic",
                "-mno-abicalls",
                "-G8",
                "-Os",
                "-fno-common",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-c",
                str(source_path),
                "-o",
                str(object_path),
            ]
        ),
        f"MIPS {object_name} overlay compile",
    )
    return object_path


def rename_mips_overlay_bss(
    arguments: argparse.Namespace,
    source_path: pathlib.Path,
    applet_name: str,
    destination_directory: pathlib.Path,
) -> pathlib.Path:
    destination_path = destination_directory / f"{applet_name}.tool.o"
    require_clean_command(
        run_command(
            [
                arguments.mips_objcopy,
                "--rename-section",
                f".bss=.app_bss_{applet_name}",
                "--rename-section",
                f".sbss=.app_bss_{applet_name}",
                str(source_path),
                str(destination_path),
            ]
        ),
        f"MIPS {applet_name} BSS rename",
    )
    return destination_path


def section_by_name(elf_image: ElfImage, section_name: str) -> SectionHeader:
    matching_sections = [
        section for section in elf_image.section_headers if section.name == section_name
    ]
    if len(matching_sections) != 1:
        raise SystemExit(
            f"expected one {section_name} section, found {len(matching_sections)}"
        )
    return matching_sections[0]


def read_aout_symbol_types(symbol_path: pathlib.Path) -> dict[str, int]:
    content = symbol_path.read_bytes()
    header = AOUT_HEADER.unpack_from(content)
    symbol_bytes = header[6]
    native_nlist_size = struct.calcsize("@PBBI")
    type_offset = struct.calcsize("@P")
    string_table_offset = AOUT_HEADER.size + symbol_bytes
    string_table_size = struct.unpack_from("<I", content, string_table_offset)[0]
    string_table_end = string_table_offset + 4 + string_table_size
    symbol_types: dict[str, int] = {}
    for symbol_offset in range(
        AOUT_HEADER.size, string_table_offset, native_nlist_size
    ):
        string_index = struct.unpack_from("<I", content, symbol_offset)[0]
        name_offset = string_table_offset + string_index
        name_end = content.find(b"\0", name_offset, string_table_end)
        if name_end < 0:
            raise SystemExit("overlay symbol has an unterminated name")
        symbol_name = content[name_offset:name_end].decode("ascii")
        symbol_types[symbol_name] = content[symbol_offset + type_offset]
    return symbol_types


def verify_multicall_object_rebuild(
    arguments: argparse.Namespace, temporary_directory: pathlib.Path
) -> None:
    rebuild_directory = temporary_directory / "rebuild"
    rebuild_directory.mkdir()
    rebuild_log = rebuild_directory / "rebuild.log"
    shared_makefile = (
        arguments.overlay_generator.parent.parent
        / "share"
        / "mk"
        / "multicall-bss-overlay.mk"
    )
    fixture_makefile = rebuild_directory / "Makefile"
    fixture_makefile.write_text(
        f"""TOPSRC={arguments.overlay_generator.parent.parent}
OBJS=alpha.tool.o
COPTS=-fcommon
all: ${{OBJS}}
include {shared_makefile}

alpha.tool.o:
\t@test "${{MULTICALL_APPLET_COPTS}}" = "-fcommon -fno-common"
\t@printf '%s\\n' rebuilt >> {rebuild_log}
\t@: > ${{.TARGET}}
""",
        encoding="ascii",
    )
    for _ in range(2):
        require_clean_command(
            run_command(
                [
                    arguments.make,
                    "-C",
                    str(rebuild_directory),
                    "-f",
                    fixture_makefile.name,
                    "all",
                ],
            ),
            "multicall object rebuild",
        )
    if rebuild_log.read_text(encoding="ascii").splitlines() != [
        "rebuilt",
        "rebuilt",
    ]:
        raise SystemExit("localized applet object was reused across box builds")


def verify_no_phdr_overlay_link(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    overlay_script: pathlib.Path,
    dispatcher_object: pathlib.Path,
    alpha_tool_object: pathlib.Path,
    beta_tool_object: pathlib.Path,
) -> None:
    no_phdr_script = temporary_directory / "no-phdr.ld"
    no_phdr_script.write_text(NO_PHDR_LINKER_SCRIPT, encoding="ascii")
    no_phdr_elf = temporary_directory / "no-phdr.elf"
    require_clean_command(
        run_command(
            [
                arguments.ld,
                "-n",
                "--no-warn-rwx-segments",
                "-T",
                str(overlay_script),
                "-T",
                str(no_phdr_script),
                "-o",
                str(no_phdr_elf),
                str(dispatcher_object),
                str(alpha_tool_object),
                str(beta_tool_object),
            ]
        ),
        "overlay link without named program headers",
    )
    no_phdr_image = parse_elf(no_phdr_elf)
    alpha_section = section_by_name(no_phdr_image, ".app_bss_alpha")
    beta_section = section_by_name(no_phdr_image, ".app_bss_beta")
    extent_section = section_by_name(no_phdr_image, ".app_bss_extent")
    if alpha_section.virtual_address != beta_section.virtual_address:
        raise SystemExit("no-PHDR applet sections do not share one address")
    if extent_section.size != max(alpha_section.size, beta_section.size):
        raise SystemExit("no-PHDR overlay extent is not the largest applet BSS")


def verify_mips_overlay_link(
    arguments: argparse.Namespace, temporary_directory: pathlib.Path
) -> None:
    mips_directory = temporary_directory / "mips"
    mips_directory.mkdir()
    dispatcher_object = compile_mips_overlay_object(
        arguments,
        mips_directory,
        "dispatcher",
        MIPS_OVERLAY_DISPATCHER_SOURCE,
    )
    alpha_object = compile_mips_overlay_object(
        arguments, mips_directory, "alpha", MIPS_OVERLAY_ALPHA_SOURCE
    )
    beta_object = compile_mips_overlay_object(
        arguments, mips_directory, "beta", MIPS_OVERLAY_BETA_SOURCE
    )
    small_bss_section = section_by_name(parse_elf(alpha_object), ".sbss")
    if small_bss_section.section_type != SHT_NOBITS or small_bss_section.size < 4:
        raise SystemExit("MIPS fixture did not create small zero-initialized storage")
    alpha_tool_object = rename_mips_overlay_bss(
        arguments, alpha_object, "alpha", mips_directory
    )
    beta_tool_object = rename_mips_overlay_bss(
        arguments, beta_object, "beta", mips_directory
    )
    require_clean_command(
        run_command(
            [
                "sh",
                str(arguments.overlay_verifier),
                arguments.mips_nm,
                arguments.mips_readelf,
                str(alpha_tool_object),
                str(beta_tool_object),
            ]
        ),
        "MIPS overlay object verification",
    )

    overlay_script = mips_directory / "multicall-bss-overlay.ld"
    require_clean_command(
        run_command(
            [
                "sh",
                str(arguments.overlay_generator),
                str(overlay_script),
                str(alpha_tool_object),
                str(beta_tool_object),
            ]
        ),
        "MIPS overlay script generation",
    )
    overlay_elf = mips_directory / "multicall-bss-overlay.elf"
    require_clean_command(
        run_command(
            [
                arguments.mips_ld,
                "-N",
                "--no-warn-rwx-segments",
                "--fatal-warnings",
                "-T",
                str(overlay_script),
                "-T",
                str(arguments.mips_linker_script),
                "-o",
                str(overlay_elf),
                str(dispatcher_object),
                str(alpha_tool_object),
                str(beta_tool_object),
            ]
        ),
        "MIPS overlay link",
    )

    elf_image = parse_elf(overlay_elf)
    alpha_section = section_by_name(elf_image, ".app_bss_alpha")
    beta_section = section_by_name(elf_image, ".app_bss_beta")
    extent_section = section_by_name(elf_image, ".app_bss_extent")
    shared_section = section_by_name(elf_image, ".bss")
    if alpha_section.virtual_address != beta_section.virtual_address:
        raise SystemExit("MIPS applet BSS sections do not share one address")
    if extent_section.virtual_address != alpha_section.virtual_address:
        raise SystemExit("MIPS loadable BSS extent does not cover the overlay")
    if extent_section.size != max(alpha_section.size, beta_section.size):
        raise SystemExit("MIPS overlay extent is not the largest applet BSS")
    if (
        shared_section.virtual_address
        < extent_section.virtual_address + extent_section.size
    ):
        raise SystemExit("MIPS shared BSS overlaps the applet BSS extent")

    overlay_aout = mips_directory / "multicall-bss-overlay.aout"
    require_clean_command(
        run_command([str(arguments.elf2aout), str(overlay_elf), str(overlay_aout)]),
        "MIPS overlay conversion",
    )
    expected_header, expected_payload = expected_aout_image(elf_image)
    overlay_content = overlay_aout.read_bytes()
    if AOUT_HEADER.unpack_from(overlay_content) != expected_header:
        raise SystemExit("MIPS overlay a.out header does not match its load segments")
    if overlay_content[AOUT_HEADER.size :] != expected_payload:
        raise SystemExit("MIPS overlay a.out payload does not match its load segments")
    if (
        expected_header[3]
        >= alpha_section.size + beta_section.size + shared_section.size
    ):
        raise SystemExit("MIPS overlay a.out BSS still sums exclusive applets")
    for initialized_word in (0x11223344, 0x55667788):
        if struct.pack("<I", initialized_word) not in expected_payload:
            raise SystemExit("MIPS overlay discarded independent initialized data")


def verify_multicall_object_rejections(
    arguments: argparse.Namespace,
    temporary_directory: pathlib.Path,
    beta_tool_object: pathlib.Path,
) -> None:
    common_object = compile_overlay_object(
        arguments,
        temporary_directory,
        "common",
        OVERLAY_COMMON_SOURCE,
    )
    common_tool_object = rename_overlay_bss(
        arguments, common_object, "common", temporary_directory
    )
    common_rejection = run_command(
        [
            "sh",
            str(arguments.overlay_verifier),
            arguments.nm,
            arguments.readelf,
            str(common_tool_object),
            str(beta_tool_object),
        ],
        expected_status=1,
    )
    if "contains COMMON storage" not in common_rejection.stderr:
        raise SystemExit("multicall verifier accepted COMMON storage")

    escaped_object = compile_overlay_object(
        arguments,
        temporary_directory,
        "escaped",
        OVERLAY_ESCAPED_NOBITS_SOURCE,
    )
    escaped_tool_object = rename_overlay_bss(
        arguments, escaped_object, "escaped", temporary_directory
    )
    escaped_rejection = run_command(
        [
            "sh",
            str(arguments.overlay_verifier),
            arguments.nm,
            arguments.readelf,
            str(escaped_tool_object),
            str(beta_tool_object),
        ],
        expected_status=1,
    )
    if "zero-initialized storage outside" not in escaped_rejection.stderr:
        raise SystemExit("multicall verifier accepted escaped NOBITS storage")

    missing_nm_rejection = run_command(
        [
            "sh",
            str(arguments.overlay_verifier),
            str(temporary_directory / "missing-nm"),
            arguments.readelf,
            str(beta_tool_object),
        ],
        expected_status=1,
    )
    if "failed for" not in missing_nm_rejection.stderr:
        raise SystemExit("multicall verifier hid an nm execution failure")

    missing_readelf_rejection = run_command(
        [
            "sh",
            str(arguments.overlay_verifier),
            arguments.nm,
            str(temporary_directory / "missing-readelf"),
            str(beta_tool_object),
        ],
        expected_status=1,
    )
    if "failed for" not in missing_readelf_rejection.stderr:
        raise SystemExit("multicall verifier hid a readelf execution failure")


def verify_multicall_bss_overlay(
    arguments: argparse.Namespace, temporary_directory: pathlib.Path
) -> None:
    dispatcher_object = compile_overlay_object(
        arguments,
        temporary_directory,
        "dispatcher",
        OVERLAY_DISPATCHER_SOURCE,
    )
    alpha_object = compile_overlay_object(
        arguments, temporary_directory, "alpha", OVERLAY_ALPHA_SOURCE
    )
    beta_object = compile_overlay_object(
        arguments, temporary_directory, "beta", OVERLAY_BETA_SOURCE
    )
    alpha_tool_object = rename_overlay_bss(
        arguments, alpha_object, "alpha", temporary_directory
    )
    beta_tool_object = rename_overlay_bss(
        arguments, beta_object, "beta", temporary_directory
    )
    require_clean_command(
        run_command(
            [
                "sh",
                str(arguments.overlay_verifier),
                arguments.nm,
                arguments.readelf,
                str(alpha_tool_object),
                str(beta_tool_object),
            ]
        ),
        "overlay object verification",
    )
    verify_multicall_object_rejections(arguments, temporary_directory, beta_tool_object)
    overlay_script = temporary_directory / "multicall-bss-overlay.ld"
    require_clean_command(
        run_command(
            [
                "sh",
                str(arguments.overlay_generator),
                str(overlay_script),
                str(alpha_tool_object),
                str(beta_tool_object),
            ]
        ),
        "overlay script generation",
    )

    overlay_elf = temporary_directory / "multicall-bss-overlay.elf"
    link_command = [
        arguments.ld,
        "-n",
        "--warn-rwx-segments",
        "--fatal-warnings",
        "-T",
        str(overlay_script),
        "-T",
        str(arguments.linker_script),
        "-o",
        str(overlay_elf),
        str(dispatcher_object),
        str(alpha_tool_object),
        str(beta_tool_object),
    ]
    require_clean_command(run_command(link_command), "overlay link")
    verify_no_phdr_overlay_link(
        arguments,
        temporary_directory,
        overlay_script,
        dispatcher_object,
        alpha_tool_object,
        beta_tool_object,
    )

    elf_image = parse_elf(overlay_elf)
    alpha_section = section_by_name(elf_image, ".app_bss_alpha")
    beta_section = section_by_name(elf_image, ".app_bss_beta")
    extent_section = section_by_name(elf_image, ".app_bss_extent")
    shared_section = section_by_name(elf_image, ".bss")
    if alpha_section.virtual_address != beta_section.virtual_address:
        raise SystemExit("applet BSS sections do not share one virtual address")
    if extent_section.virtual_address != alpha_section.virtual_address:
        raise SystemExit("loadable BSS extent does not cover the applet overlay")
    if extent_section.size != max(alpha_section.size, beta_section.size):
        raise SystemExit("loadable BSS extent is not the largest applet BSS")
    if (
        shared_section.virtual_address
        < extent_section.virtual_address + extent_section.size
    ):
        raise SystemExit("shared BSS overlaps the applet BSS extent")

    overlay_aout = temporary_directory / "multicall-bss-overlay.aout"
    require_clean_command(
        run_command([str(arguments.elf2aout), str(overlay_elf), str(overlay_aout)]),
        "overlay conversion",
    )
    expected_header, expected_payload = expected_aout_image(elf_image)
    overlay_content = overlay_aout.read_bytes()
    if AOUT_HEADER.unpack_from(overlay_content) != expected_header:
        raise SystemExit("overlay a.out header does not match its load segments")
    if overlay_content[AOUT_HEADER.size :] != expected_payload:
        raise SystemExit("overlay a.out payload does not match its load segments")
    if (
        expected_header[3]
        >= alpha_section.size + beta_section.size + shared_section.size
    ):
        raise SystemExit("overlay a.out BSS still sums mutually exclusive applets")
    for initialized_word in (0x11223344, 0x55667788):
        if struct.pack("<I", initialized_word) not in expected_payload:
            raise SystemExit("overlay discarded independent initialized data")

    overlay_symbols = temporary_directory / "multicall-bss-overlay-symbols.aout"
    require_clean_command(
        run_command(
            [
                str(arguments.elf2aout),
                "-s",
                str(overlay_elf),
                str(overlay_symbols),
            ]
        ),
        "overlay symbol conversion",
    )
    symbol_types = read_aout_symbol_types(overlay_symbols)
    for symbol_name in ("_alpha_canary", "_beta_canary"):
        if symbol_types.get(symbol_name) != N_BSS | N_EXT:
            raise SystemExit(f"{symbol_name} did not convert to external N_BSS")

    negative_directory = temporary_directory / "negative"
    negative_directory.mkdir()
    cross_reference_object = compile_overlay_object(
        arguments,
        negative_directory,
        "alpha-cross-reference",
        OVERLAY_CROSS_REFERENCE_SOURCE,
    )
    negative_alpha_object = rename_overlay_bss(
        arguments, cross_reference_object, "alpha", negative_directory
    )
    negative_script = negative_directory / "multicall-bss-overlay.ld"
    require_clean_command(
        run_command(
            [
                "sh",
                str(arguments.overlay_generator),
                str(negative_script),
                str(negative_alpha_object),
                str(beta_tool_object),
            ]
        ),
        "cross-reference script generation",
    )
    rejected_link = run_command(
        [
            arguments.ld,
            "-n",
            "--warn-rwx-segments",
            "--fatal-warnings",
            "-T",
            str(negative_script),
            "-T",
            str(arguments.linker_script),
            "-o",
            str(negative_directory / "cross-reference.elf"),
            str(dispatcher_object),
            str(negative_alpha_object),
            str(beta_tool_object),
        ],
        expected_status=1,
    )
    if "prohibited cross reference" not in rejected_link.stderr:
        raise SystemExit("NOCROSSREFS did not reject an applet BSS reference")


def main() -> int:
    arguments = parse_arguments()
    with tempfile.TemporaryDirectory(prefix="elf2aout-layout-") as directory_name:
        temporary_directory = pathlib.Path(directory_name)
        assembly_path = temporary_directory / "layout.S"
        assembly_path.write_text(ASSEMBLY_SOURCE, encoding="ascii")
        elf_paths: dict[str, pathlib.Path] = {}
        aout_images: dict[str, bytes] = {}
        for variant in LAYOUT_VARIANTS:
            elf_path = build_variant(
                arguments, temporary_directory, assembly_path, variant
            )
            elf_paths[variant.name] = elf_path
            aout_images[variant.name] = verify_conversion(arguments, elf_path, variant)

        reference_object_path = temporary_directory / "text_data_bss.o"
        verify_negative_controls(
            arguments,
            temporary_directory,
            elf_paths["text_data_bss"],
            reference_object_path,
        )
        verify_shorter_overwrite(arguments, temporary_directory, elf_paths, aout_images)
        verify_symbol_conversion(
            arguments, temporary_directory, elf_paths["text_data_bss"]
        )
        verify_multicall_bss_overlay(arguments, temporary_directory)
        verify_mips_overlay_link(arguments, temporary_directory)
        verify_multicall_object_rebuild(arguments, temporary_directory)

    print(
        "elf2aout layout: 7 variants, 3 rejection controls, "
        "shorter overwrite, symbol conversion, multicall BSS overlay, "
        "MIPS production-script overlay, object rejection, and rebuild freshness passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
