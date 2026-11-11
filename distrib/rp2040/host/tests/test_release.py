import hashlib
import os
import struct
import subprocess

import pytest

from discobsd_host import release as r


def uf2(index):
    low, high = r.REGIONS[index]
    count = (high - low) // 256 if index == 4 else 1
    output = bytearray()
    for i in range(count):
        block = bytearray(512)
        struct.pack_into(
            "<8I",
            block,
            0,
            0x0A324655,
            0x9E5D5157,
            0x2000,
            low + 256 * i,
            256,
            i,
            count,
            0xE48BFF56,
        )
        struct.pack_into("<I", block, 508, 0x0AB16F30)
        output.extend(block)
    return bytes(output)


@pytest.fixture(scope="module")
def bundle(tmp_path_factory):
    try:
        supported = b"ML-DSA-65" in r.openssl("list", "-signature-algorithms")
    except (ValueError, OSError):
        supported = False
    if not supported:
        if os.environ.get("DISCOBSD_REQUIRE_PQ_TESTS") == "1":
            pytest.fail("this gate requires OpenSSL ML-DSA-65 support")
        pytest.skip("OpenSSL ML-DSA-65 is unavailable; no hybrid result established")
    folder = tmp_path_factory.mktemp("release")
    for i, name in enumerate(r.NAMES):
        (folder / name).write_bytes(uf2(i))
    keys = {}
    for algorithm, openssl_name in (("ed25519", "ED25519"), ("mldsa65", "ML-DSA-65")):
        private, public = folder / (algorithm + ".key"), folder / (algorithm + ".pub")
        r.openssl("genpkey", "-algorithm", openssl_name, "-out", private)
        r.openssl("pkey", "-in", private, "-pubout", "-out", public)
        keys[algorithm] = public
    raw = r.make_manifest(folder, 1, 7, bytes(20), hashlib.sha256(b"test-toolchain").digest())
    manifest = folder / "release.bin"
    manifest.write_bytes(raw)
    for algorithm in keys:
        (folder / ("release.bin." + algorithm)).write_bytes(
            r.signature(raw, algorithm, folder / (algorithm + ".key"))
        )
    return folder, manifest, keys


def test_actual_hybrid_signatures_and_deterministic_manifest(bundle):
    folder, manifest, keys = bundle
    sequence, images = r.verify_bundle(folder, manifest, keys, 1, 7)
    assert sequence == 7 and len(images) == 5
    assert len(manifest.read_bytes()) == r.HEADER.size + 5 * r.RECORD.size
    assert manifest.read_bytes() == r.make_manifest(
        folder, 1, 7, bytes(20), hashlib.sha256(b"test-toolchain").digest()
    )
    for epoch, minimum in ((2, 0), (1, 8)):
        with pytest.raises(ValueError):
            r.verify_bundle(folder, manifest, keys, epoch, minimum)


@pytest.mark.parametrize("algorithm", ["ed25519", "mldsa65"])
def test_either_signature_failure_refuses(bundle, algorithm):
    folder, manifest, keys = bundle
    raw = manifest.read_bytes()
    original = (folder / ("release.bin." + algorithm)).read_bytes()
    damaged = bytes([original[0] ^ 1]) + original[1:]
    with pytest.raises(ValueError):
        r.signature(raw, algorithm, keys[algorithm], damaged)
    with pytest.raises(ValueError):
        r.signature(raw, algorithm, keys[algorithm], original[:-1])
    with pytest.raises(ValueError):
        r.verify_bundle(folder, manifest, {algorithm: keys[algorithm]}, 1, 0)


def test_artifact_mutation_and_wrong_key_type(bundle, tmp_path):
    folder, manifest, keys = bundle
    with pytest.raises(ValueError):
        r.key_type(keys["ed25519"], "mldsa65")
    for i, name in enumerate(r.NAMES):
        data = bytearray((folder / name).read_bytes())
        if i == 0:
            data[32] ^= 1
        (tmp_path / name).write_bytes(data)
    with pytest.raises(ValueError):
        r.verify_bundle(tmp_path, manifest, keys, 1, 0)


@pytest.mark.parametrize(
    "offset,value", [(8, 0x2001), (12, 0x10000100), (16, 255), (20, 1), (24, 3), (28, 0), (288, 1)]
)
def test_uf2_malformed_controls(offset, value):
    data = bytearray(uf2(0))
    struct.pack_into("<I", data, offset, value)
    with pytest.raises(ValueError):
        r.payload(data, 0)


def test_rollback_lock_and_atomic_state(tmp_path):
    path = tmp_path / "state"
    with r.rollback_lock(path) as minimum:
        assert minimum == 0
        with pytest.raises(FileExistsError):
            with r.rollback_lock(path):
                pass
        r.commit_sequence(path, 7)
    with r.rollback_lock(path) as minimum:
        assert minimum == 7
    path.write_bytes(b"bad")
    with pytest.raises(ValueError):
        with r.rollback_lock(path):
            pass


def test_verified_staging_and_failed_flash_preserve_state(bundle, tmp_path, monkeypatch):
    folder, manifest, keys = bundle
    argv = [
        "flash",
        "--manifest",
        str(manifest),
        "--directory",
        str(folder),
        "--epoch",
        "1",
        "--ed25519",
        str(keys["ed25519"]),
        "--mldsa65",
        str(keys["mldsa65"]),
        "--state",
        str(tmp_path / "state"),
        "--target",
        "PICO.uf2",
    ]
    original_run = subprocess.run
    captured = []

    def run(command, **kwargs):
        if command[0] == "discobsd-flash":
            from pathlib import Path

            assert [Path(p).name for p in command[1:]] == ["root.uf2", "PICO.uf2"]
            assert Path(command[1]).parent != folder
            assert Path(command[2]).read_bytes() == (folder / "PICO.uf2").read_bytes()
            captured.append(command)
            raise subprocess.CalledProcessError(1, command)
        return original_run(command, **kwargs)

    monkeypatch.setattr(subprocess, "run", run)
    with pytest.raises(SystemExit):
        r.main(argv)
    assert captured and not (tmp_path / "state").exists()


def test_successful_delegate_advances_state(bundle, tmp_path, monkeypatch):
    folder, manifest, keys = bundle
    original_run = subprocess.run

    def run(command, **kwargs):
        if command[0] == "discobsd-flash":
            return subprocess.CompletedProcess(command, 0)
        return original_run(command, **kwargs)

    monkeypatch.setattr(subprocess, "run", run)
    assert (
        r.main(
            [
                "flash",
                "--manifest",
                str(manifest),
                "--directory",
                str(folder),
                "--epoch",
                "1",
                "--ed25519",
                str(keys["ed25519"]),
                "--mldsa65",
                str(keys["mldsa65"]),
                "--state",
                str(tmp_path / "state"),
                "--target",
                "PICO.uf2",
            ]
        )
        == 0
    )
    assert struct.unpack("<Q", (tmp_path / "state").read_bytes())[0] == 7
