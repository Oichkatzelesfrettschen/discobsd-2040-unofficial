#!/usr/bin/env python3
"""Verify the RP2040 image keeps the root account locked and shadow private."""

from __future__ import annotations

import argparse
import pathlib
import stat
import subprocess
import tempfile


def root_password_field(passwd_data: bytes, shadow_data: bytes) -> bytes:
    passwd_lines = passwd_data.splitlines()
    root_entries = [line.split(b":") for line in passwd_lines if line.startswith(b"root:")]
    if len(root_entries) != 1 or len(root_entries[0]) < 2:
        raise ValueError("/etc/passwd must contain one valid root entry")

    try:
        offset = int(root_entries[0][1])
    except ValueError as error:
        raise ValueError("root password field must point into /etc/shadow") from error
    if offset < 0:
        raise ValueError("root password offset must be nonnegative")

    if offset >= len(shadow_data):
        raise ValueError("root password offset lies beyond /etc/shadow")

    position = 0
    shadow_roots = []
    for line in shadow_data.splitlines(keepends=True):
        fields = line.rstrip(b"\r\n").split(b":")
        if fields[0] == b"root":
            shadow_roots.append((position, fields))
        position += len(line)
    if len(shadow_roots) != 1 or len(shadow_roots[0][1]) < 2:
        raise ValueError("/etc/shadow must contain one valid root entry")
    root_position, fields = shadow_roots[0]
    if offset != root_position + len(b"root:"):
        raise ValueError("root password offset does not select the root shadow field")
    return fields[1]


def verify_account_record(shadow_mode: int, passwd_data: bytes, shadow_data: bytes) -> None:
    if shadow_mode != 0o600:
        raise ValueError(f"/etc/shadow mode is {shadow_mode:04o}, expected 0600")
    if root_password_field(passwd_data, shadow_data) != b"*":
        raise ValueError("root password field in /etc/shadow is not locked")


def verify_checker_controls() -> None:
    passwd = b"root:5:0:1:root:/root:/bin/sh\n"
    locked_shadow = b"root:*:0:1:root:/root:/bin/sh\n"
    verify_account_record(0o600, passwd, locked_shadow)

    for mode, account, shadow in (
        (0o644, passwd, locked_shadow),
        (0o600, passwd, b"root:verifier:0:1:root:/root:/bin/sh\n"),
        (0o600, b"root:4:0:1:root:/root:/bin/sh\n", locked_shadow),
    ):
        try:
            verify_account_record(mode, account, shadow)
        except ValueError:
            continue
        raise AssertionError("account image checker accepted an insecure control")


def verify_image(fsutil: pathlib.Path, image: pathlib.Path) -> None:
    with tempfile.TemporaryDirectory(prefix="rp2040-account-image-") as temporary:
        subprocess.run(
            [str(fsutil), "--extract", "--partition=1", str(image)],
            cwd=temporary,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        root = pathlib.Path(temporary)
        shadow_path = root / "etc/shadow"
        shadow_mode = stat.S_IMODE(shadow_path.stat().st_mode)
        verify_account_record(
            shadow_mode,
            (root / "etc/passwd").read_bytes(),
            shadow_path.read_bytes(),
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fsutil", type=pathlib.Path)
    parser.add_argument("image", type=pathlib.Path)
    arguments = parser.parse_args()
    verify_checker_controls()
    verify_image(arguments.fsutil.resolve(), arguments.image.resolve())
    print("RP2040 account image: root locked; /etc/shadow mode 0600")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
