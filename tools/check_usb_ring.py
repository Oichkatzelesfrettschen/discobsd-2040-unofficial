"""Exercise production USB ring enqueue accounting and calibrated controls."""

import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / "sys/arch/rp2040/dev/usb.c").read_text()
match = re.search(r"static void\nusb_tx_put\(u_char c\)\n\{.*?\n\}", source, re.S)
if match is None:
    raise SystemExit("usb ring: production enqueue function missing")
function = match.group()
fixture = r"""
#include <limits.h>
typedef unsigned int u_int;
typedef unsigned char u_char;
#define USB_TXRING 8192U
static struct { u_int tx_head, tx_tail; } usbd;
static u_char usb_tx_ring[USB_TXRING];
static u_int usb_tx_peak, usb_tx_dropped;
FUNCTION
int main(void)
{
    for (u_int i = 0; i < USB_TXRING; i++) usb_tx_put((u_char)i);
    if (usb_tx_peak != USB_TXRING || usb_tx_dropped != 0) return 1;
    usb_tx_put(42);
    if (usb_tx_dropped != 1 || usbd.tx_tail != 1 ||
        usb_tx_ring[0] != 42 || usb_tx_peak != USB_TXRING) return 2;
    usb_tx_dropped = UINT_MAX;
    usb_tx_put(43);
    if (usb_tx_dropped != UINT_MAX) return 3;
    usbd.tx_head = UINT_MAX - 1;
    usbd.tx_tail = UINT_MAX - 1;
    usb_tx_peak = usb_tx_dropped = 0;
    usb_tx_put(44); usb_tx_put(45); usb_tx_put(46);
    if (usbd.tx_head != 1 || usb_tx_peak != 3 || usb_tx_dropped != 0 ||
        usb_tx_ring[USB_TXRING - 2] != 44 ||
        usb_tx_ring[USB_TXRING - 1] != 45 || usb_tx_ring[0] != 46) return 4;
    return 0;
}
"""
variants = {
    "production": function,
    "missing-peak": function.replace(
        "usb_tx_peak = usbd.tx_head - usbd.tx_tail;", "usb_tx_peak = 0;"
    ),
    "missing-drop": function.replace("usb_tx_dropped++;", "usb_tx_dropped += 0;"),
    "wrapping-drop": function.replace("usb_tx_dropped != (u_int)-1", "1"),
}
with tempfile.TemporaryDirectory(prefix="usb-ring-") as temporary:
    directory = Path(temporary)
    for name, body in variants.items():
        cfile = directory / f"{name}.c"
        binary = directory / name
        cfile.write_text(fixture.replace("FUNCTION", body))
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror",
            str(cfile), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], check=False)
        expected = {
            "production": 0, "missing-peak": 1,
            "missing-drop": 2, "wrapping-drop": 3,
        }[name]
        if result.returncode != expected:
            raise SystemExit(f"usb ring: {name}: {result.returncode}, expected {expected}")
        print(f"usb ring: {name} passed")
