"""Route the production USB console driver's register access to a host model.

sys/arch/rp2040/dev/usb.c reaches the controller through two macros, REG32
and DPRAM8, and one inline assembly delay. The extract keeps every other
line of the driver and replaces those three definitions with calls into
usb_console_test.c, which models the register file, the SET and CLR aliases,
the write-one-to-clear BUFF_STATUS register and the DPSRAM. Each replaced
line must occur exactly once, so a driver that reaches the hardware another
way fails the extract instead of escaping the model.
"""

import argparse
import hashlib
from pathlib import Path

REPLACEMENTS = (
    (
        "#define\tREG32(a)\t(*(volatile u_int *)(a))\n",
        "#define\tREG32(a)\t(*usb_host_reg32((u_long)(a)))\n",
    ),
    (
        "#define\tDPRAM8(off)\t(*(volatile u_char *)(USB_DPRAM_BASE + (off)))\n",
        "#define\tDPRAM8(off)\t(*usb_host_dpram8((u_long)(off)))\n",
    ),
    (
        '\t__asm__ volatile ("nop; nop; nop; nop; nop; nop");\n',
        "\tusb_host_settle();\n",
    ),
)

PROLOGUE = """\
volatile u_int *usb_host_reg32(u_long address);
volatile u_char *usb_host_dpram8(u_long offset);
void usb_host_settle(void);
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    for original, replacement in REPLACEMENTS:
        if source.count(original) != 1:
            raise ValueError("driver line is not unique: " + original.strip())
        source = source.replace(original, replacement, 1)
    anchor = "#include <rp2040/dev/usb.h>\n"
    if source.count(anchor) != 1:
        raise ValueError("driver header include is not unique")
    source = source.replace(anchor, anchor + PROLOGUE, 1)
    if "volatile u_int *)" in source.replace("SCB_REG32", ""):
        raise ValueError("driver reaches a register outside REG32")

    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
        + source
    )


if __name__ == "__main__":
    main()
