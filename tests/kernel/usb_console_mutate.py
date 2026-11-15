"""Create calibrated mutations of the extracted USB console driver.

Each mutation restores one way tty output loses its wakeup: a path that ends
a bulk IN packet by clearing tx_busy alone, leaving TS_BUSY set with no
completion to clear it, or usbstart() testing the low-water mark before it
moves the queue into the ring; or usbinit() releasing the controller reset
without asserting it, so a warm reboot keeps the previous device state. A
mutation is a list of original and replacement pairs, each of which must
match once. usb_console_test must reject every one.
"""

import argparse
from pathlib import Path

MUTATIONS = {
    "skip-controller-reset": (
        "\tREG32(RESETS_BASE + RESETS_SET) = RESETS_USBCTRL;\n",
        "",
    ),
    "recover-kick": (
        "\t\tusb_tx_recovered++;\n\t\tusb_tx_idle();\n",
        "\t\tusbd.tx_busy = 0;\n\t\tusb_tx_recovered++;\n\t\tusb_tx_kick();\n",
    ),
    "configure-kick": (
        "\t/* Any packet in flight is abandoned with no completion to come. */\n\tusb_tx_idle();\n",
        "\tusbd.tx_busy = 0;\n\tusb_tx_kick();\n",
    ),
    "owake-before-drain": (
        "\tttyowake(tp);\n\tsplx(s);\n}\n",
        "\tsplx(s);\n}\n",
        "\twhile (tp->t_outq.c_cc != 0 &&\n\t    usbd.tx_head",
        "\tttyowake(tp);\n\twhile (tp->t_outq.c_cc != 0 &&\n\t    usbd.tx_head",
    ),
    "setif-kick": (
        "\t\t\tif (windex == ITF_CDC_DATA)\n\t\t\t\tusb_tx_idle();\n",
        "\t\t\tif (windex == ITF_CDC_DATA)\n\t\t\t\tusb_tx_kick();\n",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=MUTATIONS, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text()
    edits = MUTATIONS[arguments.mutation]
    for index in range(0, len(edits), 2):
        original, replacement = edits[index], edits[index + 1]
        if source.count(original) != 1:
            raise ValueError(arguments.mutation + " mutation site is not unique")
        source = source.replace(original, replacement, 1)
    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
