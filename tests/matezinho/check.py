#!/usr/bin/env python3
"""Exercise real icons, malformed volumes, storage sizes and host/QEMU parity."""

import argparse
import importlib.util
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"tools/matezinho/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", action="store_true")
    args = parser.parse_args()
    builder, storage = load("build"), load("storage")
    host = builder.build("host")
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        volume = work / "icons.mzar"
        storage.create(volume, "rom16", "assets")
        notices = work / "icons.mzar.artwork"
        assert (notices / "upstream/AUTHORS").is_file()
        assert (notices / "LICENSE-CC-BY-SA-3.0.txt").is_file()
        for profile in ("sd32", "sd32gb"):
            image = work / f"{profile}.img"
            storage.create(image, profile, "blank-sd")
            assert image.stat().st_size == storage.SIZES[profile]
            assert image.stat().st_blocks * 512 < 1024 * 1024
        # Refuse overwriting an existing image.
        try:
            storage.create(volume, "rom16", "assets")
        except FileExistsError:
            pass
        else:
            raise AssertionError("existing volume overwritten")
        invalid = work / "invalid.mzar"
        invalid.write_bytes(b"not a pack" * 2048)
        bad = subprocess.run([host, invalid, work / "bad.ppm", "0"], capture_output=True)
        assert bad.returncode != 0 and not (work / "bad.ppm").exists()
        trunc = work / "truncated.mzar"
        trunc.write_bytes(volume.read_bytes()[:1024])
        assert (
            subprocess.run([host, trunc, work / "trunc.ppm", "0"], capture_output=True).returncode
            != 0
        )
        # Each case starts at the same deterministic initial state.
        cases = [
            [],
            [257],
            [257, 10, 72, 105],
            [259, 10, 49, 50, 43, 51, 10],
            [257, 10, 72, 260, 257, 10, 263],
            [264],
            [260, 261, 257],
        ]
        frame = work / "host.ppm"
        for keys in cases:
            subprocess.run(
                [host, volume, frame, *map(str, keys or [0])], cwd=work, check=True, timeout=10
            )
            assert frame.read_bytes().startswith(b"P6\n320 320\n255\n")
            assert frame.stat().st_size == 320 * 320 * 3 + 15
        # Notes save and subsequent session load retain the file.
        subprocess.run(
            [host, volume, frame, *map(str, [257, 10, 72, 105, 265])], cwd=work, check=True
        )
        assert (work / "matezinho-note.txt").read_text() == "Hi"
        assert not (work / "matezinho-note.txt.new").exists()
        (work / "matezinho-note.txt").unlink()
        if args.qemu:
            arm = builder.build("qemu")
            qframe = work / "qemu.ppm"
            for keys in cases:
                subprocess.run(
                    [host, volume, frame, *map(str, keys or [0])], cwd=work, check=True, timeout=10
                )
                command = builder.run_command("qemu", arm, volume, qframe)
                subprocess.run([*command, *map(str, keys or [0])], cwd=work, check=True, timeout=10)
                assert frame.read_bytes() == qframe.read_bytes(), keys
            subprocess.run(
                [*builder.run_command("qemu", arm, volume, qframe), *map(str, [257, 10, 65, 265])],
                cwd=work,
                check=True,
            )
            assert (work / "matezinho-note.txt").read_text() == "A"
            too_many = [
                *builder.run_command("qemu", arm, volume, qframe),
                *("0" for _ in range(130)),
            ]
            assert subprocess.run(too_many, cwd=work).returncode == 71
            too_large = [*builder.run_command("qemu", arm, volume, qframe), "0" * 5000]
            assert subprocess.run(too_large, cwd=work).returncode == 71
            print("matezinho QEMU: seven host/ARMv6-M frames identical; note save passed")
        print("matezinho assets: provenance, refusal paths and sparse SD capacities passed")


if __name__ == "__main__":
    run()
