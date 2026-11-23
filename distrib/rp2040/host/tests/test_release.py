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
import hashlib
import os
import signal
import struct
import subprocess
import sys
import time

import pytest

from gizmobsd_host import release as r


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
        if os.environ.get("GIZMOBSD_REQUIRE_PQ_TESTS") == "1":
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
    captured = []

    def run(command, timeout):
        from pathlib import Path

        assert command[0] == "gizmobsd-flash"
        assert [Path(p).name for p in command[1:]] == ["root.uf2", "PICO.uf2"]
        assert Path(command[1]).parent != folder
        assert Path(command[2]).read_bytes() == (folder / "PICO.uf2").read_bytes()
        captured.append(command)
        raise subprocess.CalledProcessError(1, command)

    monkeypatch.setattr(r, "run_delegate", run)
    with pytest.raises(SystemExit):
        r.main(argv)
    assert captured and not (tmp_path / "state").exists()


def test_successful_delegate_advances_state(bundle, tmp_path, monkeypatch):
    folder, manifest, keys = bundle

    def run(command, timeout):
        assert command[0] == "gizmobsd-flash"

    monkeypatch.setattr(r, "run_delegate", run)
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


def _alive(pid):
    try:
        stat = open(f"/proc/{pid}/stat").read()
    except OSError:
        return False
    return stat.rsplit(")", 1)[1].split()[0] != "Z"


@pytest.mark.skipif(not os.path.isdir("/proc"), reason="needs /proc to read process state")
def test_delegate_timeout_ends_the_whole_process_group(tmp_path):
    # The script stands in for gizmobsd-flash and starts a long child the way
    # it starts picotool; a timeout must end the child too, because the caller
    # drops the rollback lock as the exception propagates.
    pidfile = tmp_path / "child.pid"
    script = tmp_path / "flash"
    script.write_text(f'#!/bin/sh\nsleep 300 &\necho $! > "{pidfile}"\nwait\n')
    script.chmod(0o755)
    with pytest.raises(subprocess.TimeoutExpired):
        r.run_delegate([str(script)], 1)
    child = int(pidfile.read_text())
    deadline = time.monotonic() + 5
    while _alive(child) and time.monotonic() < deadline:
        time.sleep(0.05)
    assert not _alive(child)


@pytest.mark.skipif(os.name != "posix", reason="the gizmobsd-flash stand-in is a #!/bin/sh script")
def test_delegate_failure_status_raises(tmp_path):
    script = tmp_path / "flash"
    script.write_text("#!/bin/sh\nexit 3\n")
    script.chmod(0o755)
    with pytest.raises(subprocess.CalledProcessError) as caught:
        r.run_delegate([str(script)], 5)
    assert caught.value.returncode == 3
    script.write_text("#!/bin/sh\nexit 0\n")
    r.run_delegate([str(script)], 5)


@pytest.mark.skipif(not os.path.isdir("/proc"), reason="needs /proc to read process state")
@pytest.mark.parametrize("name", ["SIGTERM", "SIGHUP"])
def test_signal_to_the_parent_ends_the_delegate_group(tmp_path, name):
    # Resolved here: Windows lacks SIGHUP, and a collection-time lookup
    # would fail the whole module instead of skipping this test.
    sig = getattr(signal, name)
    pidfile = tmp_path / "child.pid"
    script = tmp_path / "flash"
    script.write_text(f'#!/bin/sh\nsleep 300 &\necho $! > "{pidfile}"\nwait\n')
    script.chmod(0o755)
    code = "import sys; from gizmobsd_host import release; release.run_delegate([sys.argv[1]], 60)"
    env = dict(os.environ, PYTHONPATH=os.pathsep.join(sys.path))
    parent = subprocess.Popen([sys.executable, "-c", code, str(script)], env=env)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not (pidfile.exists() and pidfile.read_text().strip()):
        time.sleep(0.05)
    child = int(pidfile.read_text())
    assert _alive(child)
    parent.send_signal(sig)
    assert parent.wait(timeout=20) == 128 + sig
    deadline = time.monotonic() + 5
    while _alive(child) and time.monotonic() < deadline:
        time.sleep(0.05)
    assert not _alive(child)


def _pid_script(tmp_path):
    pidfile = tmp_path / "child.pid"
    script = tmp_path / "flash"
    script.write_text(f'#!/bin/sh\nsleep 300 &\necho $! > "{pidfile}"\nwait\n')
    script.chmod(0o755)
    return pidfile, script


def _wait_for_pid(pidfile):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not (pidfile.exists() and pidfile.read_text().strip()):
        time.sleep(0.05)
    return int(pidfile.read_text())


@pytest.mark.skipif(not os.path.isdir("/proc"), reason="needs /proc to read process state")
@pytest.mark.parametrize("name", ["SIGTERM", "SIGHUP", "SIGINT"])
def test_signal_between_spawn_and_handle_assignment_ends_the_group(tmp_path, monkeypatch, name):
    # The signal arrives after the delegate exists and before Popen returns to
    # run_delegate, the window in which the child handle is not yet assigned.
    # The group must still end, because the caller releases the rollback lock
    # as the exception propagates.
    import threading

    sig = getattr(signal, name)
    pidfile, script = _pid_script(tmp_path)
    real_popen = subprocess.Popen
    child = []

    def popen_then_signal(*args, **kwargs):
        proc = real_popen(*args, **kwargs)
        child.append(_wait_for_pid(pidfile))
        signal.pthread_kill(threading.get_ident(), sig)
        return proc

    monkeypatch.setattr(r.subprocess, "Popen", popen_then_signal)
    try:
        with pytest.raises((SystemExit, KeyboardInterrupt)):
            r.run_delegate([str(script)], 60)
        deadline = time.monotonic() + 5
        while _alive(child[0]) and time.monotonic() < deadline:
            time.sleep(0.05)
        assert not _alive(child[0])
    finally:
        if child and _alive(child[0]):
            os.kill(child[0], signal.SIGKILL)


@pytest.mark.skipif(not os.path.isdir("/proc"), reason="needs /proc to read process state")
def test_delegate_starts_with_no_signal_blocked(tmp_path):
    # run_delegate blocks the handled signals around the launch; the child
    # restores the mask before exec, or the delegate would ignore the SIGTERM
    # that ends its group.
    out = tmp_path / "mask"
    script = tmp_path / "flash"
    script.write_text(f'#!/bin/sh\ngrep SigBlk /proc/self/status > "{out}"\n')
    script.chmod(0o755)
    r.run_delegate([str(script)], 10)
    assert out.read_text().split()[1] == "0000000000000000"
