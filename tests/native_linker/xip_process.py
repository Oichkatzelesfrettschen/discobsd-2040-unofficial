# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Exercise diagnostic XIP startup, cross-linked callers, fork and failed exec under Renode."""

import argparse
import hashlib
import json
import runpy
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from xip_cross import cross_link
from xip_imports import RECORD, ROOT, TOOLS, link, require


def make_image(work, slot, provider, filesystem):
    record = json.loads(RECORD.with_name("compact-provider.json").read_text())
    package = provider.read_bytes()
    for data, field in ((package, "package_sha256"), (slot.read_bytes(), "slot_elf_sha256")):
        require(hashlib.sha256(data).hexdigest() == record[field], f"{field} differs")
    source = Path(__file__).with_name("xip_process.s").resolve()
    subprocess.run([TOOLS / "as", source, "-o", "start.o"], cwd=work, check=True)
    executable = link([work / "start.o"], slot).read_bytes()
    (work / "xraw").write_bytes(executable)
    damaged = bytearray(executable)
    damaged[44] ^= 1  # First byte of the required provider identity.
    (work / "badraw").write_bytes(damaged)
    (work / "shortreq").write_bytes(executable[:87])
    state = bytearray(executable)
    state[80] ^= 8  # Aligned state base misplaced by eight bytes.
    (work / "badstate").write_bytes(state)
    full = bytearray(executable)
    text, data = struct.unpack_from("<II", executable, 4)
    (extent,) = struct.unpack_from("<I", executable, 84)
    window = 192 * 1024
    struct.pack_into("<I", full, 12, window - text - data)
    struct.pack_into("<I", full, 80, 0x20000000 + window - extent)
    (work / "fullraw").write_bytes(full)
    for raw, packed in (("xraw", "xpacked"), ("badraw", "badpacked"), ("fullraw", "fullpack")):
        subprocess.run([TOOLS / "hsaout", "-c", raw, packed], cwd=work, check=True)
    (work / "shortpack").write_bytes((work / "xpacked").read_bytes()[:-1])
    files = [
        "xraw",
        "xpacked",
        "badraw",
        "badpacked",
        "badstate",
        "shortreq",
        "shortpack",
        "fullraw",
        "fullpack",
    ]
    for source, names in (
        (ROOT / "bin/echo/echo.c", ("xecho", "pecho")),
        (Path(__file__).with_name("xip_startup_test.c"), ("xcrt", "pcrt")),
    ):
        caller = work / source.stem
        caller.mkdir()
        cross_link(caller, source.resolve(), slot)
        for image, name in zip(("app.aout", "app.hsx"), names, strict=True):
            shutil.copyfile(caller / image, work / name)
            files.append(name)
    for name in files:
        (work / name).chmod(0o755)
    shutil.copyfile(filesystem, work / "root.img")
    subprocess.run(
        [TOOLS / "fsutil", "--partition=1", "--add", "root.img", *files], cwd=work, check=True
    )
    arch = ROOT / "sys/arch"
    dhara = arch / "rp2040/dhara"
    subprocess.run(
        [
            "cc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DEXEC_XIP_LIBC",
            "-I",
            arch,
            "-I",
            dhara,
            ROOT / "tools/flashimg/flashimg.c",
            *(dhara / name for name in ("error.c", "journal.c", "map.c")),
            "-o",
            "flashimg",
        ],
        cwd=work,
        check=True,
    )
    subprocess.run([work / "flashimg", "root.img", "root.bin"], cwd=work, check=True)
    root = (work / "root.bin").read_bytes()
    require(len(root) == 1488 * 1024, "diagnostic Dhara extent differs")
    (work / "flash.bin").write_bytes(package.ljust(48 * 1024, b"\xff") + root)


def boot(work, models):
    scripts = ROOT / "tools/renode"
    api = runpy.run_path(str(scripts / "check-boot.py"))
    shutil.copyfile(scripts / "pico_uart.repl", work / "pico_uart.repl")
    (work / "vendor").mkdir()
    (work / "vendor/Renode_RP2040").symlink_to(models, target_is_directory=True)
    kernel = ROOT / "sys/arch/rp2040/compile/PICO_UART_XIP/unix.bin"
    monitor = api["Renode"]("renode", str(work / "renode.log"), 300)
    transcript = work / "uart0.txt"
    console = api["Console"](monitor, str(transcript))
    try:
        for key, path in (
            ("repo_dir", models),
            ("unix_bin", kernel),
            ("board_dir", work),
            ("flash_bin", work / "flash.bin"),
        ):
            require(
                all(char.isalnum() or char in "/._-" for char in str(path)),
                "unsupported Renode monitor path",
            )
            monitor.command(f"${key}=@{path}")
        response = monitor.command(f"include @{scripts}/machine.resc")
        require("error executing command" not in response.lower(), response)
        monitor.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        console.wait_lines(
            ["(PICO_UART_XIP)", "5 programmed", "root size = 960 kbytes", "login:"],
            "diagnostic root boot",
            180,
        )
        console.type_line("operator")
        console.wait_for("$", "shell login", 60)
        for name in ("xraw", "xpacked"):
            console.type_line("/" + name)
            console.wait_lines(["XIP-FORK", "$"], name + " fork and exec rejection", 60)
        for name in ("xecho", "pecho"):
            console.type_line(f"/{name} shared process")
            console.wait_lines(["\nshared process\n", "$"], name + " argument output", 60)
        for name in ("xcrt", "pcrt"):
            console.type_line(f"/{name} {name}; echo result=$?")
            console.wait_lines(["\nresult=37\n", "$"], name + " C startup and exit", 60)
        # Known-bad arguments: each caller check must name its own exit status.
        for name in ("xcrt", "pcrt"):
            for command, status, reason in (
                (f"/{name}", 1, "missing argument"),
                (f"/{name} wrong", 2, "program name differs from argv[1]"),
            ):
                console.type_line(f"{command}; echo result=$?")
                console.wait_lines([f"\nresult={status}\n", "$"], f"{name} {reason}", 60)
    finally:
        monitor.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slot-elf", required=True, type=Path)
    parser.add_argument("--provider", required=True, type=Path)
    parser.add_argument("--filesystem", required=True, type=Path, help="partitioned 960 KiB root")
    parser.add_argument("--models", required=True, type=Path, help="built Renode_RP2040 tree")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="xip-process-", dir=ROOT / ".build-logs"))
    print(f"XIP process evidence: {work}", flush=True)
    make_image(work, args.slot_elf.resolve(), args.provider, args.filesystem)
    boot(work, args.models.resolve(strict=True))


if __name__ == "__main__":
    main()
