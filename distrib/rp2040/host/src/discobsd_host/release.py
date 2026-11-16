# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Canonical hybrid-signed RP2040 release bundles and verified staging."""

from __future__ import annotations

import argparse
import hashlib
import os
import signal
import struct
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path

MAGIC = b"DSB2040P"
DOMAIN = b"DiscoBSD-RP2040-release-manifest-v1\0"
HEADER = struct.Struct("<8sHIQ20s32s")
RECORD = struct.Struct("<BI32s32sII")
NAMES = ("PICO.uf2", "PICO_UART.uf2", "PICOCALC.uf2", "PICOCALC_PSRAM.uf2", "root.uf2")
REGIONS = ((0x10000000, 0x10020000),) * 4 + ((0x10020000, 0x101A0000),)
DER = {
    "ed25519": (bytes.fromhex("302a300506032b6570032100"), 32),
    "mldsa65": (bytes.fromhex("308207b2300b0609608648016503040312038207a100"), 1952),
}
SIGNATURE_BYTES = {"ed25519": 64, "mldsa65": 3309}
FLASH_TIMEOUT = 120


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bounded(path, limit):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, "input exceeds size limit")
    return data


def payload(data, artifact):
    require(data and len(data) % 512 == 0 and len(data) <= 4 * 1024 * 1024, "invalid UF2 size")
    count = len(data) // 512
    low, high = REGIONS[artifact]
    decoded = bytearray()
    for i in range(count):
        block = data[i * 512 : (i + 1) * 512]
        m0, m1, flags, address, size, number, total, family = struct.unpack_from("<8I", block)
        require(
            (m0, m1, struct.unpack_from("<I", block, 508)[0])
            == (0x0A324655, 0x9E5D5157, 0x0AB16F30),
            "invalid UF2 magic",
        )
        require(flags == 0x2000 and family == 0xE48BFF56, "unsupported UF2 flags/family")
        require(
            (size, number, total, address) == (256, i, count, low + 256 * i)
            and address + 256 <= high,
            "UF2 geometry/numbering mismatch",
        )
        require(block[288:508] == bytes(220), "UF2 trailing metadata is unsupported")
        decoded.extend(block[32:288])
    if artifact == 4:
        require(len(decoded) == high - low, "root UF2 does not cover its region")
    return bytes(decoded)


def make_manifest(directory, epoch, sequence, commit, toolchain):
    require(
        type(epoch) is int
        and 0 <= epoch < 2**32
        and type(sequence) is int
        and 0 < sequence < 2**64,
        "invalid epoch/sequence",
    )
    require(len(commit) == 20 and len(toolchain) == 32, "invalid commit/toolchain digest")
    output = bytearray(HEADER.pack(MAGIC, 1, epoch, sequence, commit, toolchain))
    for i, name in enumerate(NAMES):
        data = bounded(Path(directory) / name, 4 * 1024 * 1024)
        decoded = payload(data, i)
        output.extend(
            RECORD.pack(
                i,
                len(data),
                hashlib.sha256(data).digest(),
                hashlib.sha256(decoded).digest(),
                REGIONS[i][0],
                REGIONS[i][0] + len(decoded),
            )
        )
    return bytes(output)


def parse_manifest(raw, epoch, minimum):
    require(len(raw) == HEADER.size + 5 * RECORD.size, "manifest length mismatch")
    magic, version, key_epoch, sequence, _, _ = HEADER.unpack_from(raw)
    require(
        magic == MAGIC
        and version == 1
        and key_epoch == epoch
        and sequence > 0
        and sequence >= minimum,
        "manifest policy/rollback refusal",
    )
    records = []
    for i in range(5):
        record = RECORD.unpack_from(raw, HEADER.size + i * RECORD.size)
        index, size, _, _, start, end = record
        require(
            index == i
            and 0 < size <= 4 * 1024 * 1024
            and size % 512 == 0
            and start == REGIONS[i][0]
            and start < end <= REGIONS[i][1]
            and end - start == size // 2,
            "invalid artifact record",
        )
        records.append(record)
    return sequence, records


def openssl(*args):
    result = subprocess.run(["openssl", *map(str, args)], capture_output=True, timeout=30)
    require(result.returncode == 0, "OpenSSL operation failed (requires ML-DSA-65 support)")
    return result.stdout


def key_type(path, algorithm, private=False):
    args = ["pkey", "-in", path, "-pubout", "-outform", "DER"]
    if not private:
        args.append("-pubin")
    der = openssl(*args)
    prefix, size = DER[algorithm]
    require(
        der.startswith(prefix) and len(der) == len(prefix) + size, "wrong signing key algorithm"
    )


def signature(raw, algorithm, key, verify=None):
    key_type(key, algorithm, private=verify is None)
    with tempfile.TemporaryDirectory(prefix="discobsd-sign-") as folder:
        message = Path(folder) / "message"
        sig = Path(folder) / "signature"
        message.write_bytes(DOMAIN + raw)
        if verify is None:
            openssl("pkeyutl", "-sign", "-rawin", "-inkey", key, "-in", message, "-out", sig)
            output = sig.read_bytes()
            require(len(output) == SIGNATURE_BYTES[algorithm], "signature size mismatch")
            return output
        require(len(verify) == SIGNATURE_BYTES[algorithm], "signature size mismatch")
        sig.write_bytes(verify)
        openssl(
            "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey", key, "-in", message, "-sigfile", sig
        )
    return None


def verify_bundle(directory, manifest, keys, epoch, minimum):
    raw = bounded(manifest, 1024)
    sequence, records = parse_manifest(raw, epoch, minimum)
    # Both signatures authenticate precisely the same domain and manifest.
    for algorithm, key in keys.items():
        signature(
            raw,
            algorithm,
            key,
            bounded(str(manifest) + "." + algorithm, SIGNATURE_BYTES[algorithm]),
        )
    require(set(keys) == set(DER), "both pinned signing keys are required")
    images = {}
    for i, record in enumerate(records):
        data = bounded(Path(directory) / NAMES[i], 4 * 1024 * 1024)
        decoded = payload(data, i)
        require(
            len(data) == record[1]
            and hashlib.sha256(data).digest() == record[2]
            and hashlib.sha256(decoded).digest() == record[3],
            "artifact digest mismatch",
        )
        images[NAMES[i]] = data
    return sequence, images


@contextmanager
def rollback_lock(path):
    """Exclusive directory lock fails closed after crashes until owner recovery."""
    path = Path(path)
    require(path.parent.is_dir() and not path.is_symlink(), "invalid rollback path")
    if os.name == "posix":
        info = path.parent.stat()
        require(
            info.st_uid == os.getuid() and info.st_mode & 0o077 == 0,
            "rollback directory must be owned by caller with mode 0700",
        )
    lock = Path(str(path) + ".lock")
    lock.mkdir(mode=0o700)
    try:
        raw = bounded(path, 8) if path.exists() else bytes(8)
        require(len(raw) == 8, "corrupt rollback state")
        yield struct.unpack("<Q", raw)[0]
    finally:
        lock.rmdir()


def commit_sequence(path, sequence):
    path = Path(path)
    fd, temporary = tempfile.mkstemp(prefix=".release-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(struct.pack("<Q", sequence))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        if os.name == "posix":
            fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def run_delegate(command, timeout):
    """Run a flashing delegate in its own session and end the whole group.

    discobsd-flash is a script that starts picotool, so killing only the
    script on a timeout would leave picotool using the board while the caller
    released the rollback lock. The delegate leads a new process group; on
    timeout, interrupt, SIGTERM or SIGHUP the group receives SIGTERM, then
    SIGKILL, before the exception propagates. POSIX only, like the flash command.
    """

    def terminated(signum, frame):
        raise SystemExit(128 + signum)

    # The delegate runs in its own session, so a SIGTERM or SIGHUP aimed at
    # this process alone would end the parent and orphan the group; the
    # handlers turn them into an exception that takes the cleanup path below.
    previous = {}
    for sig in (signal.SIGTERM, signal.SIGHUP):
        try:
            previous[sig] = signal.signal(sig, terminated)
        except ValueError:  # not the main thread: no handler can be installed
            break
    proc = subprocess.Popen(command, start_new_session=True)
    try:
        status = proc.wait(timeout=timeout)
    except BaseException:
        for sig in (signal.SIGTERM, signal.SIGKILL):
            try:
                os.killpg(proc.pid, sig)
            except ProcessLookupError:
                break
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                continue
        proc.wait()
        raise
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    if status:
        raise subprocess.CalledProcessError(status, command)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("manifest", "sign", "verify", "flash"))
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--epoch", type=int, required=True)
    parser.add_argument("--sequence", type=int)
    parser.add_argument("--commit")
    parser.add_argument("--toolchain-sha256")
    parser.add_argument("--ed25519", type=Path)
    parser.add_argument("--mldsa65", type=Path)
    parser.add_argument("--state", type=Path)
    parser.add_argument("--target", choices=NAMES[:4])
    args = parser.parse_args(argv)
    try:
        if args.command == "manifest":
            require(
                args.directory and args.commit and args.toolchain_sha256, "missing manifest inputs"
            )
            raw = make_manifest(
                args.directory,
                args.epoch,
                args.sequence,
                bytes.fromhex(args.commit),
                bytes.fromhex(args.toolchain_sha256),
            )
            with args.manifest.open("xb") as handle:
                handle.write(raw)
        elif args.command == "sign":
            raw = bounded(args.manifest, 1024)
            parse_manifest(raw, args.epoch, 0)
            require(args.ed25519 and args.mldsa65, "two private keys required")
            for algorithm, key in (("ed25519", args.ed25519), ("mldsa65", args.mldsa65)):
                data = signature(raw, algorithm, key)
                with Path(str(args.manifest) + "." + algorithm).open("xb") as handle:
                    handle.write(data)
        else:
            require(
                args.directory and args.ed25519 and args.mldsa65 and args.state,
                "directory, pinned public keys and rollback state required",
            )
            with rollback_lock(args.state) as minimum:
                sequence, images = verify_bundle(
                    args.directory,
                    args.manifest,
                    {"ed25519": args.ed25519, "mldsa65": args.mldsa65},
                    args.epoch,
                    minimum,
                )
                if args.command == "flash":
                    require(args.target, "explicit board target required")
                    with tempfile.TemporaryDirectory(prefix="discobsd-verified-") as folder:
                        staged = []
                        for name in ("root.uf2", args.target):
                            file = Path(folder) / name
                            file.write_bytes(images[name])
                            staged.append(str(file))
                        # Delegate recovery/unmount only for private verified copies.
                        run_delegate(["discobsd-flash", *staged], FLASH_TIMEOUT)
                    commit_sequence(args.state, sequence)
                print(f"verified hybrid release sequence {sequence}")
        return 0
    except (ValueError, OSError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, f"discobsd-release: {error}\n")


if __name__ == "__main__":
    main()
