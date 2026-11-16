#!/usr/bin/env python3
"""Boot and launch MATEzinho through the PicoCalc keyboard and SPI SD card."""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import importlib.util  # noqa: E402

import picocalc_fb  # noqa: E402
from renode_monitor import Renode, monitor_int  # noqa: E402

spec = importlib.util.spec_from_file_location("picocalc_gate", HERE / "check-picocalc.py")
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)


def region_error(actual, expected, y0, y1, x0=0, x1=320):
    if len(actual) != 320 * 320 * 3 or len(expected) != len(actual):
        raise ValueError("reference and actual frames must be 320x320 RGB")
    return max(
        abs(actual[(y * 320 + x) * 3 + c] - expected[(y * 320 + x) * 3 + c])
        for y in range(y0, y1)
        for x in range(x0, x1)
        for c in range(3)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renode", default="renode")
    parser.add_argument("--flash", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host", type=Path, default=HERE.parent / "matezinho/out/matezinho.host")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    renode = Renode(args.renode, str(args.output / "renode.log"), 1200)
    run = legacy.Run(renode, str(args.output))
    try:
        renode.command(f"$flash_bin=@{args.flash.resolve()}")
        renode.command(f"$sd_image=@{args.assets.resolve()}")
        output = renode.command(f"include @{HERE / 'matezinho.resc'}")
        if "Error" in output or "exception" in output.lower():
            raise RuntimeError(output)
        run.wait_for(["login:"], "login prompt", 120)
        print("matezinho Renode: login reached", flush=True)
        run.type_line("operator")
        run.wait_for_prompt("operator shell", 30)
        command = "matezinho /dev/rsd0 /home/operator/note.txt"
        # The carrier controller holds only 31 press/release events. Drain
        # each bounded burst rather than injecting an impossible paste.
        for offset in range(0, len(command), 12):
            renode.command(f'sysbus.i2c1.kbd TypeText "{command[offset : offset + 12]}"')
            run.advance(0.5)
        renode.command("sysbus.i2c1.kbd TypeKey 10")
        if monitor_int(renode, "sysbus.i2c1.kbd Dropped") != 0:
            raise AssertionError("keyboard injection exceeded the physical FIFO")
        reference = picocalc_fb.read_ppm(str(args.reference))[2]
        matched = False
        for _ in range(100):
            run.advance(0.5)
            if any("matezinho:" in line for line in run.lines):
                raise AssertionError("desktop launch failed: " + " | ".join(run.lines[-6:]))
            width, height, pixels = picocalc_fb.read_ppm(str(args.output / "frame.ppm"))
            # Files card including original icon, caption and focus. Clock
            # pixels are deliberately outside this static comparison region.
            if region_error(pixels, reference, 30, 110, 8, 104) <= 3:
                matched = True
                picocalc_fb.write_png(str(args.output / "desktop.png"), width, height, pixels)
                break
        if not matched:
            raise AssertionError("desktop Files card did not match host reference")
        reads = monitor_int(renode, "sysbus.spi0.sdcard Reads")
        if reads <= 0:
            raise AssertionError("desktop did not read icons through modeled SPI SD")
        renode.command("sysbus.i2c1.kbd TypeKey 183")  # Right
        renode.command("sysbus.i2c1.kbd TypeKey 10")
        run.advance(1)
        run.snapshot("notes")
        renode.command('sysbus.i2c1.kbd TypeText "Hello PicoCalc"')
        run.advance(1)
        renode.command("sysbus.spi1.lcd DumpPpm @" + str(args.output / "notes-text.ppm"))
        actual = picocalc_fb.read_ppm(str(args.output / "notes-text.ppm"))[2]
        with tempfile.TemporaryDirectory() as directory:
            expected_path = Path(directory) / "notes.ppm"
            keys = [257, 10, *(ord(c) for c in "Hello PicoCalc")]
            subprocess.run(
                [
                    str(args.host.resolve()),
                    str(args.assets.resolve()),
                    str(expected_path),
                    *map(str, keys),
                ],
                cwd=directory,
                check=True,
                timeout=10,
            )
            expected = picocalc_fb.read_ppm(str(expected_path))[2]
        if region_error(actual, expected, 30, 320) > 3:
            raise AssertionError("Notes input did not reproduce the reference view")
        print(
            f"matezinho Renode: original icon card and typed Notes matched; "
            f"{reads} SPI sector reads",
            flush=True,
        )
    finally:
        renode.close()


if __name__ == "__main__":
    main()
