"""Create one calibrated defect in the production in-place exchange."""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"{mutation}: expected one mutation site")
    return source.replace(old, new, 1)


def mutate(source: str, mutation: str) -> str:
    if mutation == "skip-sector-erase":
        return replace_once(
            source,
            "            SWAP_IMAGE_WRITE) != 0)",
            "            B_WRITE) != 0)",
            mutation,
        )
    if mutation == "write-u-after-reading-incoming":
        return replace_once(
            source,
            "outgoing_segment < 3;",
            "outgoing_segment < 2;",
            mutation,
        )
    if mutation == "allocate-tail-after-first-write":
        return replace_once(
            source,
            "    if (! swap_exchange_tail_reserve (incoming->p_daddr,",
            "    if (0 && ! swap_exchange_tail_reserve (incoming->p_daddr,",
            mutation,
        )
    if mutation == "ignore-program-fault":
        old = """        if (swap_with_buf_result (bp,
            flash_base + btod (image_start), window_start, length,
            SWAP_IMAGE_WRITE) != 0)
            panic (\"swap exchange: media fault\");"""
        new = """        (void) swap_with_buf_result (bp,
            flash_base + btod (image_start), window_start, length,
            SWAP_IMAGE_WRITE);"""
        return replace_once(source, old, new, mutation)
    if mutation == "copy-buffer-before-program":
        old = """        if (candidate < outgoing_sectors)
            swap_exchange_program_sector (bp, reservation->addr[0],
                candidate, outgoing_segments);
        if (candidate < incoming_sectors)
            swap_exchange_scatter_sector (candidate, incoming_segments);"""
        new = """        if (candidate < incoming_sectors)
            swap_exchange_scatter_sector (candidate, incoming_segments);
        if (candidate < outgoing_sectors)
            swap_exchange_program_sector (bp, reservation->addr[0],
                candidate, outgoing_segments);"""
        return replace_once(source, old, new, mutation)
    if mutation == "refuse-owned-tail":
        old = """        reservation->exchange_span = 0;
        if (swap_image_reserve_flash (reservation) != 0)
            return ENOMEM;
        reservation->state = SWAP_IMAGE_RESERVED;
        return 0;"""
        return replace_once(source, old, "        return ENOMEM;", mutation)
    if mutation == "load-data-over-clean-text":
        return replace_once(
            source,
            "        (size_t) __user_data_start + text;",
            "        (size_t) __user_data_start;",
            mutation,
        )
    if mutation == "tail-ignores-ring-end":
        return replace_once(
            source,
            "            end -= swapmap->m_ring_end - swapmap->m_ring_first;",
            "            end -= 0;",
            mutation,
        )
    raise ValueError(f"unknown mutation: {mutation}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", required=True)
    arguments = parser.parse_args()
    arguments.output.write_text(mutate(arguments.source.read_text(), arguments.mutation))


if __name__ == "__main__":
    main()
