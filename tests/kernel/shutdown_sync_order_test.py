"""Verify shutdown synchronization precedes final reset handling."""

import sys
from pathlib import Path


class OrderError(Exception):
    """A required shutdown ordering edge is absent."""


def boot_body(source_text):
    function_start = source_text.index(
        "\nvoid\nboot(dev_t dev __unused, int howto)\n"
    )
    function_end = source_text.index(
        "\n/*\n * Millisecond delay routine.", function_start
    )
    return source_text[function_start:function_end]


def require_order(source_text):
    body = boot_body(source_text)
    required_prefix = (
        "\nvoid\nboot(dev_t dev __unused, int howto)\n"
        "{\n"
        "\tif ((howto & RB_NOSYNC) == 0 && waittime < 0 &&\n"
        "\t    bfreelist[0].av_forw != NULL)\n"
        "\t\trp2040_shutdown_sync(howto);\n"
        "\t(void)splhigh();\n"
    )
    call_text = "\t\trp2040_shutdown_sync(howto);"
    mask_text = "\t(void)splhigh();"
    bootloader_text = "\tif (howto & RB_BOOTLOADER) {"
    if not body.startswith(required_prefix):
        raise OrderError(
            "boot must begin with the complete shutdown admission block and "
            "final interrupt mask"
        )
    if body.count(call_text) != 1:
        raise OrderError("boot must call rp2040_shutdown_sync exactly once")
    call_index = body.index(call_text)
    mask_index = body.index(mask_text)
    bootloader_index = body.index(bootloader_text)
    if not call_index < mask_index < bootloader_index:
        raise OrderError(
            "shutdown synchronization must precede final masking and reset"
        )


def expect_rejection(source_text, old_text, new_text, mutation_name):
    if source_text.count(old_text) != 1:
        raise OrderError(f"{mutation_name}: mutation anchor is ambiguous")
    mutated_text = source_text.replace(old_text, new_text, 1)
    try:
        require_order(mutated_text)
    except (OrderError, ValueError):
        return
    raise OrderError(f"{mutation_name}: changed ordering was accepted")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: shutdown_sync_order_test.py MACHDEP_SOURCE")
    source_text = Path(sys.argv[1]).read_text(encoding="utf-8")
    require_order(source_text)
    expect_rejection(
        source_text,
        "\t\trp2040_shutdown_sync(howto);\n\t(void)splhigh();",
        "\t(void)splhigh();\n\t\trp2040_shutdown_sync(howto);",
        "mask-before-sync",
    )
    expect_rejection(
        source_text,
        "\t\trp2040_shutdown_sync(howto);\n",
        "",
        "missing-sync-call",
    )
    expect_rejection(
        source_text,
        "(howto & RB_NOSYNC) == 0",
        "(howto & RB_NOSYNC) != 0",
        "inverted-no-sync-guard",
    )
    expect_rejection(
        source_text,
        " && waittime < 0",
        "",
        "missing-reentry-guard",
    )
    expect_rejection(
        source_text,
        " &&\n\t    bfreelist[0].av_forw != NULL",
        "",
        "missing-buffer-list-guard",
    )
    expect_rejection(
        source_text,
        "boot(dev_t dev __unused, int howto)\n{\n",
        "boot(dev_t dev __unused, int howto)\n{\n"
        "\tif (howto & RB_HALT)\n"
        "\t\treturn;\n",
        "early-exit-before-admission",
    )
    print("PASS shutdown synchronization precedes final reset handling")


if __name__ == "__main__":
    main()
