#!/usr/bin/env python3
"""Build bounded MATE artwork volumes and sparse development SD images."""

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "share/matezinho/mate"
SIZES = {
    "compact": 19456,
    "rom16": 16 * 1024**2,
    "sd32": 32 * 1024**2,
    "sd32gb": (32_000_000_000 // 524288) * 524288,
}


def verify():
    ledger = json.loads((ART / "provenance.json").read_text())
    for name, evidence in ledger["files"].items():
        data = (ART / "upstream" / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != evidence["sha256"]:
            raise ValueError(f"upstream hash mismatch: {name}")
    return ledger


def artwork():
    ledger = verify()
    data = bytearray(1024 + 9 * 2048)
    data[:16] = b"MZAR0001\x09\x18\x18\x02\x00\x00\x00\x00"
    for i, icon in enumerate(ledger["icons"]):
        with Image.open(ART / "upstream" / icon["png"]) as image:
            if image.size != (24, 24):
                raise ValueError("icons must be native 24x24")
            # Flatten against the renderer's RGB565 0x2124 surface.
            base = Image.new("RGBA", (24, 24), (33, 36, 33, 255))
            base.alpha_composite(image.convert("RGBA"))
            rgb = base.convert("RGB").tobytes()
            for j in range(24 * 24):
                r, g, b = rgb[j * 3 : j * 3 + 3]
                value = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
                off = 1024 + i * 2048 + j * 2
                data[off : off + 2] = value.to_bytes(2, "little")
    return data


def create(path, profile, kind):
    size = SIZES[profile]
    # A separate immutable asset volume is never confused with a guest filesystem.
    data = artwork() if kind == "assets" else b""
    if data:
        data[12:16] = (size // 512).to_bytes(4, "little")
    with path.open("xb") as stream:
        if data:
            stream.write(data)
        stream.truncate(size)
        stream.flush()
        os.fsync(stream.fileno())
    if kind == "assets":
        # Every generated volume travels with exact source art and notices.
        # Refuse an existing companion directory rather than replacing it.
        companion = path.with_name(path.name + ".artwork")
        created = False
        try:
            companion.mkdir()
            created = True
            shutil.copytree(ART, companion, dirs_exist_ok=True)
        except Exception:
            path.unlink()
            if created:
                shutil.rmtree(companion)
            raise
    if profile == "rom16" and kind == "assets":
        path.chmod(0o444)
    return {
        "profile": profile,
        "kind": kind,
        "bytes": size,
        "payload_bytes": len(data),
        "payload_sha256": hashlib.sha256(data).hexdigest(),
        "sparse": True,
        "guest_filesystem": None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile", choices=SIZES, default="rom16")
    parser.add_argument("--kind", choices=("assets", "blank-sd"), default="assets")
    args = parser.parse_args()
    info = create(args.output, args.profile, args.kind)
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
