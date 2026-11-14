#!/usr/bin/env python3
"""Exercise real icons, malformed volumes, storage sizes and host/QEMU parity."""

import argparse
import hashlib
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


ART = ROOT / "third_party/mate-icon-theme"

# Sizes MATEZINHO.md states, held apart from storage.SIZES so that a changed
# profile table fails here.
PROFILE_BYTES = {"rom16": 16777216, "sd32": 33554432, "sd32gb": 31999918080}
ROW = 320 * 3
HEADER = 15


def require(condition, message):
    """Fail the gate by an exit that python -O keeps."""
    if not condition:
        raise SystemExit(f"matezinho check: {message}")


def result_band(frame):
    """Pixel rows 137-145, where the calculator view draws its result."""
    data = frame.read_bytes()
    return data[HEADER + 137 * ROW : HEADER + 146 * ROW]


def artwork_ledger_errors(art):
    """Compare every file under the artwork root with its README.md row."""
    rows = {}
    for line in (art / "README.md").read_text(encoding="utf-8").splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) == 4 and cells[0].startswith("`") and cells[0].endswith("`"):
            rows[cells[0].strip("`")] = cells[3].strip("`")
    files = {
        path.relative_to(art).as_posix(): path
        for path in art.rglob("*")
        if path.is_file() and path.relative_to(art).as_posix() != "README.md"
    }
    errors = [f"no README.md row: {name}" for name in sorted(files.keys() - rows.keys())]
    errors += [f"row names a missing file: {name}" for name in sorted(rows.keys() - files.keys())]
    for name in sorted(files.keys() & rows.keys()):
        digest = hashlib.sha256(files[name].read_bytes()).hexdigest()
        if digest != rows[name]:
            errors.append(f"sha256 differs from README.md row: {name}")
    return errors


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", action="store_true")
    args = parser.parse_args()
    errors = artwork_ledger_errors(ART)
    if errors:
        raise SystemExit("matezinho artwork ledger: " + "; ".join(errors))
    builder, storage = load("build"), load("storage")
    host = builder.build("host")
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        volume = work / "icons.mzar"
        storage.create(volume, "rom16", "assets")
        notices = work / "icons.mzar.artwork"
        for name in ("upstream/AUTHORS", "LICENSES/CC-BY-SA-3.0.txt", "README.md"):
            require((notices / name).is_file(), f"companion lacks {name}")
        require(volume.stat().st_size == PROFILE_BYTES["rom16"], "rom16 volume size")
        require(volume.stat().st_mode & 0o222 == 0, "rom16 asset volume is writable")
        for profile in ("sd32", "sd32gb"):
            image = work / f"{profile}.img"
            storage.create(image, profile, "blank-sd")
            require(image.stat().st_size == PROFILE_BYTES[profile], f"{profile} image size")
            require(image.stat().st_blocks * 512 < 1024 * 1024, f"{profile} image is not sparse")
        # Refuse overwriting an existing image.
        try:
            storage.create(volume, "rom16", "assets")
        except FileExistsError:
            pass
        else:
            raise SystemExit("matezinho check: existing volume overwritten")
        invalid = work / "invalid.mzar"
        invalid.write_bytes(b"not a pack" * 2048)
        bad = subprocess.run([host, invalid, work / "bad.ppm", "0"], capture_output=True)
        require(bad.returncode != 0 and not (work / "bad.ppm").exists(), "invalid volume accepted")
        trunc = work / "truncated.mzar"
        trunc.write_bytes(volume.read_bytes()[:1024])
        require(
            subprocess.run([host, trunc, work / "trunc.ppm", "0"], capture_output=True).returncode
            != 0,
            "truncated volume accepted",
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
            require(frame.read_bytes().startswith(b"P6\n320 320\n255\n"), f"frame header {keys}")
            require(frame.stat().st_size == 320 * ROW + HEADER, f"frame size {keys}")
        # The result line of each expression equals that of a sum with the
        # same value, and differs from that of a sum with another value.
        bands = {}
        for text in ("42+0", "13+0", "9-4", "5+0", "6*7", "84/2", "7/0", "99999+1", "6+7"):
            subprocess.run(
                [host, volume, frame, "259", "10", *(str(ord(c)) for c in text), "10"],
                cwd=work,
                check=True,
                timeout=10,
            )
            bands[text] = result_band(frame)
        require(bands["42+0"] != bands["13+0"], "result band does not show the value")
        for text, same in (("9-4", "5+0"), ("6*7", "42+0"), ("84/2", "42+0"), ("6+7", "13+0")):
            require(bands[text] == bands[same], f"calculator: {text} differs from {same}")
        require(bands["7/0"] == bands["99999+1"], "calculator: 7/0 is not refused")
        require(bands["7/0"] != bands["42+0"], "calculator: refusal reads as a value")
        # Notes save and subsequent session load retain the file.
        subprocess.run(
            [host, volume, frame, *map(str, [257, 10, 72, 105, 265])], cwd=work, check=True
        )
        require((work / "matezinho-note.txt").read_text() == "Hi", "saved note text")
        require(not (work / "matezinho-note.txt.new").exists(), "note temporary left behind")
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
                require(frame.read_bytes() == qframe.read_bytes(), f"host/QEMU frames {keys}")
            subprocess.run(
                [*builder.run_command("qemu", arm, volume, qframe), *map(str, [257, 10, 65, 265])],
                cwd=work,
                check=True,
            )
            require((work / "matezinho-note.txt").read_text() == "A", "QEMU note text")
            too_many = [
                *builder.run_command("qemu", arm, volume, qframe),
                *("0" for _ in range(130)),
            ]
            require(subprocess.run(too_many, cwd=work).returncode == 71, "argument count bound")
            too_large = [*builder.run_command("qemu", arm, volume, qframe), "0" * 5000]
            require(subprocess.run(too_large, cwd=work).returncode == 71, "argument size bound")
            print("matezinho QEMU: seven host/ARMv6-M frames identical; note save passed")
        print("matezinho assets: provenance, refusal paths and sparse SD capacities passed")


if __name__ == "__main__":
    run()
