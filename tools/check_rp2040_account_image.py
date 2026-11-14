#!/usr/bin/env python3
"""Verify the RP2040 image's account records, shadow privacy and privileged inodes.

The privileged inodes are the set-user-id programs and the kernel memory
boundary: /dev/mem, /dev/kmem and /dev/swap read and write kernel state, so
they are mode 0640 in group kmem. /bin/ps reads the kernel's process
records through sysctl(3), and /usr/bin/fstat the kernel's descriptor
records, so neither carries a set-id bit or the kmem group.
Group 0 is wheel, which lists operator.
"""

from __future__ import annotations

import argparse
import io
import pathlib
import stat
import subprocess
import tempfile

KMEM_GROUP = 2

# Path to (file type, permission bits, owner, group), as fsutil -vvv prints them.
PRIVILEGED_INODES = {
    "/usr/bin/passwd": (stat.S_IFREG, 0o4755, 0, 0),
    "/usr/bin/su": (stat.S_IFREG, 0o4751, 0, 0),
    "/bin/ps": (stat.S_IFREG, 0o775, 0, 0),
    "/usr/bin/fstat": (stat.S_IFREG, 0o775, 0, 0),
    "/dev/mem": (stat.S_IFCHR, 0o640, 0, KMEM_GROUP),
    "/dev/kmem": (stat.S_IFCHR, 0o640, 0, KMEM_GROUP),
    "/dev/swap": (stat.S_IFBLK, 0o640, 0, KMEM_GROUP),
}

InodeRecord = tuple[int, int, int, int]


def account_password_field(passwd_data: bytes, shadow_data: bytes, account: bytes) -> bytes:
    # A NUL hides the consumed newline from scanpw(), which then discards
    # the following record while searching for a newline in the stream.
    if b"\0" in passwd_data:
        raise ValueError("/etc/passwd contains a NUL that changes libc record parsing")
    # scanpw() strips LF alone, leaving CR in fields such as the shell path.
    if b"\r" in passwd_data:
        raise ValueError("/etc/passwd contains CR bytes that remain in libc fields")
    name = account.decode("ascii")
    records = [line for line in io.BytesIO(passwd_data) if line.split(b":", 1)[0] == account]
    if len(records) != 1:
        raise ValueError(f"/etc/passwd must contain one valid {name} entry")
    record = records[0]
    # scanpw() requires the newline to fit in its 256-byte fgets buffer.
    if not record.endswith(b"\n") or len(record) > 255:
        raise ValueError(f"{name} passwd record exceeds the libc parser contract")
    passwd_fields = record[:-1].split(b":")
    if len(passwd_fields) != 7:
        raise ValueError(f"/etc/passwd must contain one valid {name} entry")
    if not passwd_fields[1].isdigit():
        raise ValueError(f"{name} password field must be a decimal shadow offset")
    offset = int(passwd_fields[1])
    position = 0
    shadow_entries = []
    for line in io.BytesIO(shadow_data):
        fields = line.rstrip(b"\r\n").split(b":")
        if fields[0] == account:
            shadow_entries.append((position, fields))
        position += len(line)
    if len(shadow_entries) != 1 or len(shadow_entries[0][1]) != 7:
        raise ValueError(f"/etc/shadow must contain one valid {name} entry")
    account_position, fields = shadow_entries[0]
    if offset != account_position + len(account) + 1:
        raise ValueError(f"{name} password offset does not select its shadow field")
    return fields[1]


def verify_account_record(shadow_mode: int, passwd_data: bytes, shadow_data: bytes) -> None:
    if shadow_mode != 0o600:
        raise ValueError(f"/etc/shadow mode is {shadow_mode:04o}, expected 0600")
    if account_password_field(passwd_data, shadow_data, b"root") != b"*":
        raise ValueError("root password field in /etc/shadow is not locked")
    if account_password_field(passwd_data, shadow_data, b"operator") != b"":
        raise ValueError("operator password field in /etc/shadow must be empty")


def privileged_inode_metadata(fsutil_output: str) -> dict[str, InodeRecord]:
    """Return the type, permission bits, owner and group of each privileged path."""
    lines = fsutil_output.splitlines()
    metadata = {}
    for index, line in enumerate(lines):
        path = next(
            (candidate for candidate in PRIVILEGED_INODES if line.startswith(candidate + " - ")),
            None,
        )
        if path is None:
            continue
        mode = None
        owner = None
        group = None
        for detail in lines[index + 1 :]:
            if detail.startswith("/"):
                break
            if detail.startswith("       Mode: "):
                mode = int(detail.split(":", 1)[1].strip(), 8)
            elif detail.startswith("   Owner id: "):
                owner = int(detail.split(":", 1)[1].strip())
            elif detail.startswith("   Group id: "):
                group = int(detail.split(":", 1)[1].strip())
        if mode is not None and owner is not None and group is not None:
            metadata[path] = (stat.S_IFMT(mode), stat.S_IMODE(mode), owner, group)
    return metadata


def verify_privileged_inodes(metadata: dict[str, InodeRecord]) -> None:
    for path, expected in PRIVILEGED_INODES.items():
        actual = metadata.get(path)
        if actual is None:
            raise ValueError(f"{path} inode metadata is missing")
        if actual != expected:
            raise ValueError(
                f"{path} type/mode/owner/group is {actual[0]:o}/{actual[1]:04o}/"
                f"{actual[2]}/{actual[3]}, expected {expected[0]:o}/{expected[1]:04o}/"
                f"{expected[2]}/{expected[3]}"
            )


def verify_checker_controls() -> None:
    root_shadow = b"root:*:0:1:root:/root:/bin/sh\n"
    operator_shadow = b"operator::2:5:System &:/operator:/bin/sh\n"
    operator_offset = len(root_shadow) + len(b"operator:")
    root_passwd = b"root:5:0:1:root:/root:/bin/sh\n"
    operator_passwd = f"operator:{operator_offset}:2:5:System &:/operator:/bin/sh\n".encode()
    passwd = root_passwd + operator_passwd
    locked_shadow = root_shadow + operator_shadow
    verify_account_record(0o600, passwd, locked_shadow)

    # Offset validation follows record position, rather than a fixed byte number.
    relocated_shadow = operator_shadow + root_shadow
    relocated_passwd = (
        f"root:{len(operator_shadow) + len(b'root:')}:0:1:root:/root:/bin/sh\n"
        "operator:9:2:5:System &:/operator:/bin/sh\n"
    ).encode()
    verify_account_record(0o600, relocated_passwd, relocated_shadow)

    boundary_passwd = passwd.replace(
        b"System &", b"x" * (255 - len(operator_passwd) + len(b"System &"))
    )
    verify_account_record(0o600, boundary_passwd, locked_shadow)
    invalid_records = [
        (0o600, passwd.replace(b"\n", b"\r\n"), locked_shadow),
        (
            0o600,
            passwd.replace(b"\noperator:", b"\nsys:\0*:4:2:System:/tmp:nologin\noperator:", 1),
            locked_shadow,
        ),
        (0o600, passwd.replace(b"\noperator:", b"\njunk\roperator:", 1), locked_shadow),
        (0o600, passwd.rstrip(b"\n"), locked_shadow),
        (0o600, boundary_passwd.replace(b":2:5:", b":2:5:x"), locked_shadow),
        (0o600, passwd.replace(b"System &", b"System\0&"), locked_shadow),
        (0o644, passwd, locked_shadow),
        (0o600, passwd, locked_shadow.replace(b"root:*:", b"root:x:")),
        (0o600, passwd.replace(b"root:5:", b"root:4:"), locked_shadow),
        (0o600, passwd, locked_shadow.replace(b"operator::", b"operator:*:")),
        (0o600, root_passwd, locked_shadow),
        (0o600, operator_passwd, locked_shadow),
        (0o600, passwd + root_passwd, locked_shadow),
        (0o600, passwd, operator_shadow),
        (0o600, passwd, locked_shadow + root_shadow),
        (0o600, passwd + operator_passwd, locked_shadow),
        (0o600, passwd, root_shadow),
        (0o600, passwd, locked_shadow + operator_shadow),
        (0o600, passwd, b"\n" + locked_shadow),
    ]
    for invalid_offset in (b"0", b"-1", b"*", b"99999", str(operator_offset + 12).encode()):
        invalid_records.append(
            (
                0o600,
                passwd.replace(
                    b"operator:" + str(operator_offset).encode() + b":",
                    b"operator:" + invalid_offset + b":",
                ),
                locked_shadow,
            )
        )
    for mode, account, shadow in invalid_records:
        try:
            verify_account_record(mode, account, shadow)
        except ValueError:
            continue
        raise AssertionError("account image checker accepted an invalid control")

    listing = "".join(
        f"{path} - 123 bytes\n       Mode: {kind | mode:#o}\n"
        f"            READ\n      Links: 1\n   Owner id: {owner}\n   Group id: {group}\n"
        for path, (kind, mode, owner, group) in PRIVILEGED_INODES.items()
    )
    packed_metadata = privileged_inode_metadata(listing)
    verify_privileged_inodes(packed_metadata)
    # A listing without the group line leaves the record incomplete.
    if "/dev/mem" in privileged_inode_metadata(listing.replace("   Group id: 2\n", "")):
        raise AssertionError("account image checker read a record without its group")
    unsafe_metadata = (
        ("/usr/bin/passwd", (stat.S_IFREG, 0o775, 0, 0)),
        ("/usr/bin/su", (stat.S_IFREG, 0o751, 0, 0)),
        ("/usr/bin/passwd", (stat.S_IFREG, 0o4755, 2, 0)),
        ("/bin/ps", (stat.S_IFREG, 0o2751, 0, KMEM_GROUP)),
        ("/bin/ps", (stat.S_IFREG, 0o2775, 0, 0)),
        ("/bin/ps", (stat.S_IFREG, 0o4775, 0, 0)),
        ("/bin/ps", (stat.S_IFREG, 0o775, 0, KMEM_GROUP)),
        ("/usr/bin/fstat", (stat.S_IFREG, 0o2755, 0, KMEM_GROUP)),
        ("/usr/bin/fstat", (stat.S_IFREG, 0o2775, 0, 0)),
        ("/usr/bin/fstat", (stat.S_IFREG, 0o775, 0, KMEM_GROUP)),
        ("/dev/mem", (stat.S_IFCHR, 0o664, 0, 0)),
        ("/dev/mem", (stat.S_IFCHR, 0o640, 0, 0)),
        ("/dev/kmem", (stat.S_IFCHR, 0o660, 0, KMEM_GROUP)),
        ("/dev/kmem", (stat.S_IFREG, 0o640, 0, KMEM_GROUP)),
        ("/dev/swap", (stat.S_IFBLK, 0o644, 0, KMEM_GROUP)),
        ("/dev/swap", (stat.S_IFCHR, 0o640, 0, KMEM_GROUP)),
        ("/dev/swap", (stat.S_IFBLK, 0o640, 2, KMEM_GROUP)),
    )
    for path, unsafe_record in unsafe_metadata:
        altered_metadata = dict(packed_metadata)
        altered_metadata[path] = unsafe_record
        try:
            verify_privileged_inodes(altered_metadata)
        except ValueError:
            continue
        raise AssertionError(f"account image checker accepted unsafe inode metadata for {path}")
    for path in PRIVILEGED_INODES:
        missing_metadata = dict(packed_metadata)
        del missing_metadata[path]
        try:
            verify_privileged_inodes(missing_metadata)
        except ValueError:
            continue
        raise AssertionError(f"account image checker accepted a missing {path}")


def verify_image(fsutil: pathlib.Path, image: pathlib.Path) -> None:
    listing = subprocess.run(
        [str(fsutil), "-vvv", "--partition=1", str(image)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    verify_privileged_inodes(privileged_inode_metadata(listing))

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
    print(
        "RP2040 account image: root locked; operator empty; /etc/shadow mode 0600; "
        "passwd, su, ps and the kernel memory devices verified"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
