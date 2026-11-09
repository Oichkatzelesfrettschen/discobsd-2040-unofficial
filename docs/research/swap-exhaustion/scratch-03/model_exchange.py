#!/usr/bin/env python
"""Model the exact LARGE image as an in-place NOR sector exchange."""

from __future__ import annotations

from dataclasses import dataclass

PROGRAM_BYTES = 256
SECTOR_BYTES = 4096
DEVICE_BLOCK_BYTES = 1024
ERASED_PAGE = b"\xff" * PROGRAM_BYTES

TEXT_BYTES = 8040
DATA_BYTES = 159744
DATA_AFTER_TEXT_BYTES = DATA_BYTES - TEXT_BYTES
STACK_BYTES = 2244
USER_AREA_BYTES = 3072


def round_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


@dataclass(frozen=True)
class Segment:
    name: str
    image_offset: int
    length: int


DATA_BLOCKS = round_up(DATA_AFTER_TEXT_BYTES, DEVICE_BLOCK_BYTES) // DEVICE_BLOCK_BYTES
STACK_BLOCKS = round_up(STACK_BYTES, DEVICE_BLOCK_BYTES) // DEVICE_BLOCK_BYTES
USER_AREA_BLOCKS = round_up(USER_AREA_BYTES, DEVICE_BLOCK_BYTES) // DEVICE_BLOCK_BYTES
IMAGE_BLOCKS = round_up(
    DATA_BLOCKS + STACK_BLOCKS + USER_AREA_BLOCKS,
    SECTOR_BYTES // DEVICE_BLOCK_BYTES,
)
IMAGE_BYTES = IMAGE_BLOCKS * DEVICE_BLOCK_BYTES
IMAGE_SECTORS = IMAGE_BYTES // SECTOR_BYTES

SEGMENTS = (
    Segment("data-after-text", 0, DATA_AFTER_TEXT_BYTES),
    Segment("stack", DATA_BLOCKS * DEVICE_BLOCK_BYTES, STACK_BYTES),
    Segment(
        "user-area",
        (DATA_BLOCKS + STACK_BLOCKS) * DEVICE_BLOCK_BYTES,
        USER_AREA_BYTES,
    ),
)


class InjectedFailure(Exception):
    def __init__(self, operation: str, position: int, mode: str) -> None:
        super().__init__(f"{operation} failure {mode} callback {position}")
        self.operation = operation
        self.position = position
        self.mode = mode


class NorViolation(Exception):
    pass


@dataclass
class ExchangeState:
    current_sector: int = -1
    completed_sectors: int = 0
    bounce: bytes = b""
    metadata_committed: bool = False
    extent_released: bool = False


class NorDevice:
    def __init__(
        self,
        incoming_image: bytes,
        failure_operation: str | None = None,
        failure_position: int = 0,
        failure_mode: str = "before",
    ) -> None:
        self.image_base = SECTOR_BYTES
        self.storage = bytearray([0xA5] * (IMAGE_BYTES + 2 * SECTOR_BYTES))
        self.storage[self.image_base : self.image_base + IMAGE_BYTES] = incoming_image
        self.failure_operation = failure_operation
        self.failure_position = failure_position
        self.failure_mode = failure_mode
        self.erase_calls = 0
        self.program_calls = 0
        self.read_bytes = 0

    def _failure_matches(self, operation: str, position: int) -> bool:
        return (
            operation == self.failure_operation
            and position == self.failure_position
        )

    def read(self, offset: int, length: int) -> bytes:
        self.read_bytes += length
        return bytes(self.storage[offset : offset + length])

    def erase(self, offset: int) -> None:
        if offset % SECTOR_BYTES != 0:
            raise NorViolation("unaligned erase")
        self.erase_calls += 1
        if self._failure_matches("erase", self.erase_calls):
            if self.failure_mode == "after":
                self.storage[offset : offset + SECTOR_BYTES] = b"\xff" * SECTOR_BYTES
            raise InjectedFailure("erase", self.erase_calls, self.failure_mode)
        self.storage[offset : offset + SECTOR_BYTES] = b"\xff" * SECTOR_BYTES

    def program(self, offset: int, source: bytes) -> None:
        if offset % PROGRAM_BYTES != 0 or len(source) != PROGRAM_BYTES:
            raise NorViolation("unaligned program")
        old_bytes = self.storage[offset : offset + PROGRAM_BYTES]
        if old_bytes != ERASED_PAGE and any(
            (old_byte & new_byte) != new_byte
            for old_byte, new_byte in zip(old_bytes, source)
        ):
            raise NorViolation("program attempted a zero-to-one transition")
        self.program_calls += 1
        programmed = source
        if self._failure_matches("program", self.program_calls):
            if self.failure_mode == "after":
                self.storage[offset : offset + PROGRAM_BYTES] = programmed
            raise InjectedFailure("program", self.program_calls, self.failure_mode)
        self.storage[offset : offset + PROGRAM_BYTES] = programmed

    def assert_guards(self) -> None:
        assert self.storage[: self.image_base] == bytes([0xA5] * SECTOR_BYTES)
        image_end = self.image_base + IMAGE_BYTES
        assert self.storage[image_end:] == bytes([0xA5] * SECTOR_BYTES)


def make_payload(length: int, seed: int) -> bytes:
    return bytes(((index * 37 + seed) % 251) for index in range(length))


def make_resident(seed: int) -> dict[str, bytearray]:
    return {
        segment.name: bytearray(make_payload(segment.length, seed + index * 41))
        for index, segment in enumerate(SEGMENTS)
    }


def make_image(resident: dict[str, bytearray]) -> bytes:
    image = bytearray(b"\xff" * IMAGE_BYTES)
    for segment in SEGMENTS:
        image[segment.image_offset : segment.image_offset + segment.length] = resident[
            segment.name
        ]
    return bytes(image)


def sector_payload_pages(sector_index: int) -> tuple[int, ...]:
    sector_start = sector_index * SECTOR_BYTES
    pages: list[int] = []
    for page_offset in range(0, SECTOR_BYTES, PROGRAM_BYTES):
        page_start = sector_start + page_offset
        page_end = page_start + PROGRAM_BYTES
        if any(
            page_start < segment.image_offset + segment.length
            and segment.image_offset < page_end
            for segment in SEGMENTS
        ):
            pages.append(page_offset)
    return tuple(pages)


def gather_sector(resident: dict[str, bytearray], sector_index: int) -> bytes:
    sector_start = sector_index * SECTOR_BYTES
    sector_end = sector_start + SECTOR_BYTES
    gathered = bytearray(b"\xff" * SECTOR_BYTES)
    for segment in SEGMENTS:
        segment_start = segment.image_offset
        segment_end = segment_start + segment.length
        overlap_start = max(sector_start, segment_start)
        overlap_end = min(sector_end, segment_end)
        if overlap_start >= overlap_end:
            continue
        source_start = overlap_start - segment_start
        destination_start = overlap_start - sector_start
        gathered[destination_start : destination_start + overlap_end - overlap_start] = resident[
            segment.name
        ][source_start : source_start + overlap_end - overlap_start]
    return bytes(gathered)


def scatter_sector(
    resident: dict[str, bytearray], sector_index: int, saved_sector: bytes
) -> None:
    sector_start = sector_index * SECTOR_BYTES
    sector_end = sector_start + SECTOR_BYTES
    for segment in SEGMENTS:
        segment_start = segment.image_offset
        segment_end = segment_start + segment.length
        overlap_start = max(sector_start, segment_start)
        overlap_end = min(sector_end, segment_end)
        if overlap_start >= overlap_end:
            continue
        source_start = overlap_start - sector_start
        destination_start = overlap_start - segment_start
        resident[segment.name][
            destination_start : destination_start + overlap_end - overlap_start
        ] = saved_sector[source_start : source_start + overlap_end - overlap_start]


def assert_resident_sector(
    resident: dict[str, bytearray], expected_image: bytes, sector_index: int
) -> None:
    assert gather_sector(resident, sector_index) == expected_image[
        sector_index * SECTOR_BYTES : (sector_index + 1) * SECTOR_BYTES
    ]


def exchange(
    device: NorDevice,
    resident: dict[str, bytearray],
    state: ExchangeState,
    *,
    scatter_before_program: bool = False,
    skip_erase: bool = False,
    commit_each_sector: bool = False,
    release_before_exchange: bool = False,
) -> None:
    if release_before_exchange:
        state.extent_released = True
    for sector_index in range(state.completed_sectors, IMAGE_SECTORS):
        state.current_sector = sector_index
        flash_offset = device.image_base + sector_index * SECTOR_BYTES
        state.bounce = device.read(flash_offset, SECTOR_BYTES)
        if scatter_before_program:
            scatter_sector(resident, sector_index, state.bounce)
        outgoing_sector = gather_sector(resident, sector_index)
        if not skip_erase:
            device.erase(flash_offset)
        if device.read(flash_offset, SECTOR_BYTES) != b"\xff" * SECTOR_BYTES:
            raise NorViolation("erase verification failed")
        for page_offset in sector_payload_pages(sector_index):
            device.program(
                flash_offset + page_offset,
                outgoing_sector[page_offset : page_offset + PROGRAM_BYTES],
            )
        if device.read(flash_offset, SECTOR_BYTES) != outgoing_sector:
            raise NorViolation("program verification failed")
        if not scatter_before_program:
            scatter_sector(resident, sector_index, state.bounce)
        state.completed_sectors += 1
        if commit_each_sector:
            state.metadata_committed = True
    state.metadata_committed = True


def prepare_completed_prefix(
    device: NorDevice,
    resident: dict[str, bytearray],
    incoming_image: bytes,
    outgoing_image: bytes,
    state: ExchangeState,
    completed_sectors: int,
) -> None:
    for sector_index in range(completed_sectors):
        flash_start = device.image_base + sector_index * SECTOR_BYTES
        image_start = sector_index * SECTOR_BYTES
        device.storage[flash_start : flash_start + SECTOR_BYTES] = outgoing_image[
            image_start : image_start + SECTOR_BYTES
        ]
        scatter_sector(
            resident,
            sector_index,
            incoming_image[image_start : image_start + SECTOR_BYTES],
        )
    state.completed_sectors = completed_sectors
    device.erase_calls = completed_sectors
    device.program_calls = sum(
        len(sector_payload_pages(sector_index))
        for sector_index in range(completed_sectors)
    )


def fresh_case(
    failure_operation: str | None = None,
    failure_position: int = 0,
    failure_mode: str = "before",
) -> tuple[
    NorDevice,
    dict[str, bytearray],
    bytes,
    dict[str, bytearray],
    bytes,
    ExchangeState,
]:
    incoming_resident = make_resident(19)
    outgoing_resident = make_resident(113)
    incoming_image = make_image(incoming_resident)
    outgoing_image = make_image(outgoing_resident)
    device = NorDevice(
        incoming_image,
        failure_operation,
        failure_position,
        failure_mode,
    )
    state = ExchangeState()
    return (
        device,
        outgoing_resident,
        outgoing_image,
        incoming_resident,
        incoming_image,
        state,
    )


def run_success() -> tuple[int, int, int]:
    device, resident, outgoing_image, _, incoming_image, state = fresh_case()
    exchange(device, resident, state)
    assert make_image(resident) == incoming_image
    assert device.read(device.image_base, IMAGE_BYTES) == outgoing_image
    assert state.metadata_committed
    assert not state.extent_released
    device.assert_guards()
    return device.erase_calls, device.program_calls, device.read_bytes


def run_failure_matrix(operation: str, operation_count: int) -> int:
    checked = 0
    for position in range(1, operation_count + 1):
        for failure_mode in ("before", "after"):
            device, resident, outgoing_image, _, incoming_image, state = fresh_case(
                operation, position, failure_mode
            )
            if operation == "erase":
                current_sector = position - 1
            else:
                cumulative_programs = 0
                current_sector = 0
                for sector_index in range(IMAGE_SECTORS):
                    cumulative_programs += len(sector_payload_pages(sector_index))
                    if position <= cumulative_programs:
                        current_sector = sector_index
                        break
            prepare_completed_prefix(
                device,
                resident,
                incoming_image,
                outgoing_image,
                state,
                current_sector,
            )
            try:
                exchange(device, resident, state)
            except InjectedFailure as failure:
                assert failure.operation == operation
                assert failure.position == position
            else:
                raise AssertionError("injected operation did not fail")
            assert not state.metadata_committed
            assert not state.extent_released
            current_sector = state.current_sector
            incoming_sector = incoming_image[
                current_sector * SECTOR_BYTES : (current_sector + 1) * SECTOR_BYTES
            ]
            assert state.bounce == incoming_sector
            assert_resident_sector(resident, outgoing_image, current_sector)
            for completed_sector in range(state.completed_sectors):
                assert_resident_sector(resident, incoming_image, completed_sector)
                flash_start = device.image_base + completed_sector * SECTOR_BYTES
                expected_outgoing = outgoing_image[
                    completed_sector * SECTOR_BYTES : (completed_sector + 1) * SECTOR_BYTES
                ]
                assert device.read(flash_start, SECTOR_BYTES) == expected_outgoing
            for untouched_sector in range(current_sector + 1, IMAGE_SECTORS):
                assert_resident_sector(resident, outgoing_image, untouched_sector)
                flash_start = device.image_base + untouched_sector * SECTOR_BYTES
                expected_incoming = incoming_image[
                    untouched_sector * SECTOR_BYTES : (untouched_sector + 1) * SECTOR_BYTES
                ]
                assert device.read(flash_start, SECTOR_BYTES) == expected_incoming
            device.assert_guards()
            checked += 1
    return checked


def reject_mutants() -> tuple[str, ...]:
    rejected: list[str] = []

    device, resident, outgoing_image, _, _, state = fresh_case()
    exchange(device, resident, state, scatter_before_program=True)
    if device.read(device.image_base, IMAGE_BYTES) != outgoing_image:
        rejected.append("scatter-before-program")

    device, resident, _, _, _, state = fresh_case()
    try:
        exchange(device, resident, state, skip_erase=True)
    except (NorViolation, InjectedFailure):
        rejected.append("program-without-erase")

    device, resident, _, _, _, state = fresh_case("program", 17, "before")
    try:
        exchange(device, resident, state, commit_each_sector=True)
    except InjectedFailure:
        if state.metadata_committed:
            rejected.append("commit-each-sector")

    device, resident, _, _, _, state = fresh_case("erase", 1, "before")
    try:
        exchange(device, resident, state, release_before_exchange=True)
    except InjectedFailure:
        if state.extent_released:
            rejected.append("release-before-commit")

    unrounded_offsets = (0, DATA_AFTER_TEXT_BYTES, DATA_AFTER_TEXT_BYTES + STACK_BYTES)
    if any(offset % DEVICE_BLOCK_BYTES != 0 for offset in unrounded_offsets):
        rejected.append("unrounded-segment-boundaries")

    full_sector_programs = IMAGE_SECTORS * (SECTOR_BYTES // PROGRAM_BYTES)
    expected_programs = sum(
        len(sector_payload_pages(sector_index))
        for sector_index in range(IMAGE_SECTORS)
    )
    if full_sector_programs != expected_programs:
        rejected.append("program-all-padding-pages")

    expected = {
        "scatter-before-program",
        "program-without-erase",
        "commit-each-sector",
        "release-before-commit",
        "unrounded-segment-boundaries",
        "program-all-padding-pages",
    }
    assert set(rejected) == expected
    return tuple(rejected)


def main() -> None:
    expected_programs = sum(
        len(sector_payload_pages(sector_index))
        for sector_index in range(IMAGE_SECTORS)
    )
    erase_calls, program_calls, read_bytes = run_success()
    assert erase_calls == IMAGE_SECTORS
    assert program_calls == expected_programs == 614
    erase_failures = run_failure_matrix("erase", erase_calls)
    program_failures = run_failure_matrix("program", program_calls)
    mutants = reject_mutants()

    print(
        "geometry: "
        f"data_after_text={DATA_AFTER_TEXT_BYTES} at 0 "
        f"stack={STACK_BYTES} at {SEGMENTS[1].image_offset} "
        f"uarea={USER_AREA_BYTES} at {SEGMENTS[2].image_offset}"
    )
    print(
        f"image: blocks={IMAGE_BLOCKS} bytes={IMAGE_BYTES} sectors={IMAGE_SECTORS} "
        f"program_pages={program_calls}"
    )
    print(
        "boundary sectors: "
        f"37_pages={len(sector_payload_pages(37))} "
        f"38_pages={len(sector_payload_pages(38))}"
    )
    print(
        f"success: erases={erase_calls} programs={program_calls} "
        f"xip_read_bytes={read_bytes - IMAGE_BYTES}"
    )
    print(
        f"failure matrix: erase={erase_failures} program={program_failures} "
        "modes=before,after metadata_commits=0 adjacent_damage=0"
    )
    print(f"mutants rejected: {','.join(mutants)}")


if __name__ == "__main__":
    main()
