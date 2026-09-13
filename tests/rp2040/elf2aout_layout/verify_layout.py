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
    parser.add_argument("--linker-script", type=pathlib.Path, required=True)
    parser.add_argument("--elf2aout", type=pathlib.Path, required=True)
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
    if program_header_size != PROGRAM_HEADER.size:
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

    print(
        "elf2aout layout: 7 variants, 3 rejection controls, "
        "shorter overwrite, and symbol conversion passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
