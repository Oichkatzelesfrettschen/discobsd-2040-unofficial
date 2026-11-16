#!/usr/bin/env python3
"""Board test for usr.sbin/v7fs: the tests/v7fs_core tool session on silicon.

usage: DISCOBSD_BOARD_V7FS=run PYTHON board_v7fs.py ORACLE PATTERN_DIR OUT_DIR

The script touches the board only when DISCOBSD_BOARD_V7FS is exactly "run";
unset, empty and every other value exit without opening a port. It needs a
board already running a root that carries /usr/sbin/v7fs (stage
"file /usr/sbin/v7fs" in distrib/rp2040/mi.rp2040 for the image build and
revert it before committing). ORACLE is the v7fs_core program
tests/v7fs_core/check.sh builds, and PATTERN_DIR holds the pattern files it
writes with "v7fs_core DIR - patterns".

It logs in as operator (the console refuses root), uploads the three
patterns (compress -b 12 and uuencode, paced by echo so the console input
queue never overflows), and runs the gate's tool session in both byte
orders on a 2048-sector, 64-inode image in /tmp. Each populated image comes
back compressed and uuencoded and must satisfy the oracle's verify mode, and
every df and ls result must equal the gate's expected text. OUT_DIR
receives raw.log (the whole console stream), results.txt and both images.
"""

import glob
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import serial

OPT_IN_VARIABLE = "DISCOBSD_BOARD_V7FS"
OPT_IN_VALUE = "run"
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")
# The sentinel is spelled __DO''NE__ in the command, so only the shell's
# output matches; an echoed ^D can precede it on the same line.
SENTINEL = re.compile(rb"__DONE__ (\d+)\s*$", re.M)
DF_BEFORE = "blocks 2048 data 2038 free 1953 inodes 64 free 58"
DF_AFTER = "blocks 2048 data 2038 free 1956 inodes 64 free 60"
LS_NAMES = ". .. hello big sub/"


class Console:
    def __init__(self, port: str, log: Path) -> None:
        self.port = serial.Serial(port, 115200, timeout=0.2)
        self.log = log.open("ab")
        self.buffer = b""

    def wait(self, pattern: bytes, seconds: float) -> bytes:
        end = time.time() + seconds
        while time.time() < end:
            chunk = self.port.read(4096)
            if chunk:
                self.log.write(chunk)
                self.log.flush()
                self.buffer += chunk
                clean = ANSI.sub(b"", self.buffer).replace(b"\r", b"")
                if re.search(pattern, clean, re.M):
                    self.buffer = b""
                    return clean
        raise TimeoutError(f"no {pattern!r} within {seconds}s")

    def send(self, text: str, per_byte: float = 0.02) -> None:
        for byte in text.encode():
            self.port.write(bytes([byte]))
            time.sleep(per_byte)

    def run(self, command: str, seconds: float = 120) -> tuple[int, str]:
        self.buffer = b""
        self.send(command + "; echo __DO''NE__ $?\r")
        clean = self.wait(SENTINEL.pattern, seconds)
        status = int(SENTINEL.findall(clean)[-1])
        lines = clean.decode(errors="replace").split("\n")[1:]
        body = [ln for ln in lines if "__DONE__" not in ln and "@pico:" not in ln]
        return status, "\n".join(body).strip()


def upload(con: Console, local: Path, remote: str) -> None:
    packed = subprocess.run(
        ["compress", "-b", "12", "-c", str(local)], capture_output=True, check=True
    ).stdout
    encoded = subprocess.run(
        ["uuencode", remote + ".Z"], input=packed, capture_output=True, check=True
    ).stdout.decode()
    con.buffer = b""
    con.send(f"cat > {remote}.uu; echo __DO''NE__ $?\r")
    time.sleep(0.5)
    for line in encoded.splitlines():
        con.buffer = b""
        con.send(line + "\n", per_byte=0.003)
        con.wait(re.escape(line[-12:].encode()) + rb"\s*$", 20)
    con.buffer = b""
    con.port.write(b"\x04")
    con.wait(SENTINEL.pattern, 60)
    for cmd in (
        f"cd /tmp && uudecode {remote}.uu",
        f"uncompress -f {remote}.Z",
        f"rm -f {remote}.uu",
    ):
        status, out = con.run(cmd)
        if status != 0:
            raise RuntimeError(f"{cmd}: status {status}: {out}")
    _, out = con.run(f"cksum {remote}")
    host = subprocess.run(
        ["cksum", str(local)], capture_output=True, check=True, text=True
    ).stdout.split()[:2]
    board = next(ln.split()[:2] for ln in out.splitlines() if ln.endswith(remote))
    if board != host:
        raise RuntimeError(f"{remote}: board cksum {board}, host {host}")


def download(con: Console, remote: str, local: Path) -> None:
    status, out = con.run(f"compress -b 12 -c {remote} | uuencode x.Z", 300)
    if status != 0:
        raise RuntimeError(f"download {remote}: status {status}")
    text = out[out.index("begin ") : out.rindex("\nend") + 4] + "\n"
    work = local.parent / (local.name + ".dl")
    work.mkdir(exist_ok=True)
    (work / "x.uu").write_text(text.replace("\n\n", "\n"))
    subprocess.run(["uudecode", "-o", str(work / "x.Z"), str(work / "x.uu")], check=True)
    # The board's uuencode records mode 0, which the host decode keeps.
    (work / "x.Z").chmod(0o600)
    local.write_bytes(
        subprocess.run(
            ["uncompress", "-c", str(work / "x.Z")], capture_output=True, check=True
        ).stdout
    )


def main() -> int:
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print(f"board_v7fs: {OPT_IN_VARIABLE}={OPT_IN_VALUE} not set; board untouched")
        return 0
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    oracle, patterns, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    ports = sorted(glob.glob(PORT_PATTERN))
    if len(ports) != 1:
        raise SystemExit(f"board_v7fs: need one console matching {PORT_PATTERN}")
    out.mkdir(parents=True, exist_ok=True)
    con = Console(ports[0], out / "raw.log")
    failures: list[str] = []

    with (out / "results.txt").open("w") as results:

        def step(cmd: str, want: int = 0) -> str:
            status, text = con.run(cmd)
            results.write(f"== {cmd} status={status}\n{text}\n")
            results.flush()
            if status != want:
                failures.append(f"{cmd}: status {status}, wanted {want}")
            return text

        con.send("\r")
        seen = con.wait(rb"(login: |[#$] $)", 90)
        if b"login: " in seen:
            con.send("operator\r")
            seen = con.wait(rb"(Password:|[#$] $)", 30)
            if b"Password:" in seen:
                con.send("\r")
                con.wait(rb"[#$] $", 30)
        # An interrupted session leaves its files in /tmp on the root.
        step("cd /tmp && rm -f *.img *.uu *.Z *.out hello big inner")
        for cmd in ("uname -a", "cat /etc/release", "df", "cksum /usr/sbin/v7fs"):
            step(cmd)
        for name in ("hello", "big", "inner"):
            upload(con, patterns / name, f"/tmp/{name}")
        for order in ("little", "pdp"):
            tool = "/usr/sbin/v7fs" + (" -p" if order == "pdp" else "")
            img = f"/tmp/{order}.img"
            step(f"{tool} newfs {img} 2048 64")
            step(f"{tool} put {img} /tmp/hello /hello")
            step(f"{tool} put {img} /tmp/big big")
            step(f"{tool} mkdir {img} /sub")
            step(f"{tool} put {img} /tmp/inner /sub/inner")
            names = " ".join(ln.split()[-1] for ln in step(f"{tool} ls {img}").splitlines())
            if names != LS_NAMES:
                failures.append(f"ls ({order}): {names!r}")
            if step(f"{tool} df {img}") != DF_BEFORE:
                failures.append(f"df before removal ({order})")
            step(f"{tool} get {img} /big /tmp/big.out")
            step("cmp /tmp/big /tmp/big.out")
            step(f"{tool} cat {img} sub/inner > /tmp/inner.out")
            step("cmp /tmp/inner /tmp/inner.out")
            local = out / f"board-{order}.img"
            download(con, img, local)
            verify = subprocess.run(
                [str(oracle), str(local), order, "verify"],
                capture_output=True,
                text=True,
                check=False,
            )
            results.write(f"== oracle {order} status={verify.returncode}\n{verify.stdout}\n")
            if verify.returncode != 0:
                failures.append(f"oracle ({order}): {verify.stdout.strip()}")
            step(f"{tool} rm {img} sub/inner")
            step(f"{tool} rm {img} sub")
            if step(f"{tool} df {img}") != DF_AFTER:
                failures.append(f"df after removal ({order})")
            step(f"{tool} frobnicate {img}", want=2)
            step(f"rm -f {img} /tmp/big.out /tmp/inner.out")
    for failure in failures:
        print(f"board_v7fs: FAIL {failure}")
    if failures:
        return 1
    print("board_v7fs: PASS (both byte orders; df, ls and oracle agree)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
