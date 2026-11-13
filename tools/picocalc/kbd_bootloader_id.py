"""Read the PicoCalc keyboard controller's identity through its USART1
system bootloader, sending only commands that read.

The controller (U201, CKS32F103Rx in LQFP64) enters its system bootloader
when BOOT0 is high at reset; SW701 in its keyboard-flash position raises
BOOT0 and routes the CH340C bridge to M_UART1 (PA9/PA10), so the host sees
the bootloader on the CH340C's tty. The bootloader speaks the STM32 USART
protocol (ST application note AN3155): 8 data bits, even parity, one stop
bit, a 0x7F byte to detect the baud rate, ACK 0x79, NACK 0x1F, and every
command byte followed by its complement.

ALLOWED_COMMANDS is the whole set this tool can send: Get (0x00), Get
Version and Read Protection Status (0x01), Get ID (0x02) and Read Memory
(0x11). send_command() refuses any other byte, so no path in this file can
reach Write, Erase, Go, Write Protect or Readout Protect/Unprotect; the
last two rewrite the option bytes and Readout Unprotect erases the flash.

Reads, in order:

    Get ID                  product ID: 0x410 medium density (x8, xB),
                            0x414 high density (xC, xD, xE) on ST parts
    0x1FFFF7E0, 2 bytes     flash size in Kbytes (RM0008 30.1)
    0x1FFFF7E8, 12 bytes    unique device ID (RM0008 30.2)
    0x1FFFF800, 16 bytes    option bytes (RM0008 Table 4 and its peers,
                            information block): RDP, USER, WRP
    0xE000ED00, 4 bytes     CPUID: 0x411FC231 r1p1 (DDI0337E, CPUID
                            Base Register); 0x412FC231 r2p1, the same
                            fields with variant 2 and revision 1
    0xE0042000, 4 bytes     DBGMCU_IDCODE: DEV_ID and REV_ID
                            (RM0008 31.6.1)

A NACK after the address or the length is the bootloader rejecting that
frame; a NACK of the Read Memory command byte itself is read as readout
protection (hypothesis: AN3155, which the notes repository does not hold).
A bootloader that leaves Read Memory out of Get's command list would
refuse the same byte for another reason, so the memory reads run only
when Get answers and lists 0x11. The flash-size word decides the verdict;
the CPUID and DBGMCU_IDCODE reads are attempts recorded apart, because an
ST bootloader may refuse the private peripheral bus and ES096 2.2.2 makes
DBGMCU_IDCODE read 0x00000000 outside debug mode on ST silicon, so a
nonzero value is itself an observation about the CKS part.

--dump FILE copies the application flash from 0x08000000 through the same
Read Memory command, 256 bytes per request, for the length the
flash-size word gives (or a --dump-length no larger), and records the
copy's sha256: a backup of the installed keyboard firmware and the bytes
that identify its version. FILE must not exist yet, so a rerun never
replaces an earlier copy.
The vendor wiki's own session (`Setting-Up-Arduino-Development-for-PicoCalc-
keyboard.md` in the notes repository) shows 57600 baud 8E1, bootloader
version 0x22 and device ID 0x0410, so 57600 is the default rate.

Exit status: 0 when Get ID and the flash-size word both read; 3 when Get
lists Read Memory, Get ID answers and the flash-size read is refused at
its command byte (hypothesis: readout protection); 1 for any other
refusal of that read, Read Memory absent from Get's list, any protocol
failure (a silent port and an ambiguous sync included; the
session then sends nothing more), an unopenable port and a dump that
cannot complete; 2 for a usage error, a missing pyserial, an existing
dump file and a --dump-length outside 1 to the flash size. The record is
printed as JSON on standard output.

Usage: $PYTHON tools/picocalc/kbd_bootloader_id.py /dev/ttyUSB0
Calibration: $PYTHON tools/picocalc/kbd_bootloader_id_test.py
"""

import argparse
import hashlib
import json
import os
import sys
import time

ACK = 0x79
NACK = 0x1F
SYNC = 0x7F

CMD_GET = 0x00
CMD_GET_VERSION = 0x01
CMD_GET_ID = 0x02
CMD_READ_MEMORY = 0x11
ALLOWED_COMMANDS = frozenset({CMD_GET, CMD_GET_VERSION, CMD_GET_ID, CMD_READ_MEMORY})

FLASH_SIZE_ADDR = 0x1FFFF7E0
UID_ADDR = 0x1FFFF7E8
OPTION_BYTES_ADDR = 0x1FFFF800
CPUID_ADDR = 0xE000ED00
DBGMCU_IDCODE_ADDR = 0xE0042000
FLASH_BASE = 0x08000000
READ_CHUNK = 256

PRODUCT_IDS = {0x410: "medium density (x8, xB)", 0x414: "high density (xC, xD, xE)"}
CPUID_REVISIONS = {0x411FC231: "Cortex-M3 r1p1", 0x412FC231: "Cortex-M3 r2p1"}

EXIT_OK = 0
EXIT_PROTOCOL = 1
EXIT_USAGE = 2
EXIT_PROTECTED = 3


class ProtocolError(Exception):
    """The bootloader answered outside the protocol or not at all."""


class Refused(Exception):
    """The bootloader answered NACK to a well-formed request.

    stage names the byte it refused: "command" for the command byte, which
    readout protection refuses for Read Memory, or "address", "length" or
    "end" for a later frame. After a NACK the bootloader waits for a new
    command, so the session may continue.
    """

    def __init__(self, what, stage):
        super().__init__(what)
        self.stage = stage


class Bootloader:
    """One bootloader session over a port that offers read(n) and write(b).

    read(n) returns at most n bytes and fewer, possibly none, on timeout,
    which is pyserial's contract for a port opened with a timeout.
    """

    def __init__(self, port):
        self.port = port

    def _read_exact(self, n, what):
        data = self.port.read(n)
        if len(data) != n:
            raise ProtocolError(f"{what}: expected {n} bytes, received {len(data)}")
        return data

    def _expect_ack(self, what, stage):
        b = self._read_exact(1, what)[0]
        if b == NACK:
            raise Refused(what, stage)
        if b != ACK:
            raise ProtocolError(f"{what}: byte 0x{b:02x} is neither ACK nor NACK")

    def sync(self):
        """Send the baud-rate detection byte and leave the parser waiting
        for a command byte.

        A fresh bootloader answers 0x7F with ACK. One synced by an earlier
        session takes 0x7F as a command byte and waits for its complement,
        so a silent first byte gets a second 0x7F, whose failed complement
        check draws NACK and returns the parser to its command state; a
        NACK to either byte marks the parser idle. An ACK to the second
        byte is ambiguous: it may be the first byte's late ACK with the
        second 0x7F still pending as a command byte, so the session stops,
        and a rerun's first 0x7F completes that pending byte with NACK.
        """
        self.port.write(bytes([SYNC]))
        reply = self.port.read(1)
        retried = not reply
        if retried:
            self.port.write(bytes([SYNC]))
            reply = self.port.read(1)
        if len(reply) != 1:
            raise ProtocolError("sync: no reply to two 0x7F bytes")
        b = reply[0]
        if b not in (ACK, NACK):
            raise ProtocolError(f"sync: byte 0x{b:02x} is neither ACK nor NACK")
        if retried and b == ACK:
            raise ProtocolError("sync: an ACK after the second 0x7F may be the first's; run again")

    def send_command(self, cmd):
        if cmd not in ALLOWED_COMMANDS:
            raise ValueError(f"command 0x{cmd:02x} is outside the read-only set")
        self.port.write(bytes([cmd, cmd ^ 0xFF]))
        self._expect_ack(f"command 0x{cmd:02x}", "command")

    def get(self):
        self.send_command(CMD_GET)
        n = self._read_exact(1, "get length")[0]
        body = self._read_exact(n + 1, "get body")
        self._expect_ack("get end", "end")
        return {"version": body[0], "commands": list(body[1:])}

    def get_version(self):
        self.send_command(CMD_GET_VERSION)
        body = self._read_exact(3, "get version body")
        self._expect_ack("get version end", "end")
        return {"version": body[0], "option_bytes": [body[1], body[2]]}

    def get_id(self):
        self.send_command(CMD_GET_ID)
        n = self._read_exact(1, "get id length")[0]
        body = self._read_exact(n + 1, "get id body")
        self._expect_ack("get id end", "end")
        return int.from_bytes(body, "big")

    def read_memory(self, addr, length):
        if not 1 <= length <= 256:
            raise ValueError(f"read length {length} is outside 1..256")
        self.send_command(CMD_READ_MEMORY)
        a = addr.to_bytes(4, "big")
        self.port.write(a + bytes([a[0] ^ a[1] ^ a[2] ^ a[3]]))
        self._expect_ack(f"read address 0x{addr:08x}", "address")
        self.port.write(bytes([length - 1, (length - 1) ^ 0xFF]))
        self._expect_ack(f"read length at 0x{addr:08x}", "length")
        return self._read_exact(length, f"read data at 0x{addr:08x}")


class SessionStopped(Exception):
    """A protocol failure left the bootloader's parser state unknown."""


def attempt(record, key, fn):
    """Run one read and record its value or refusal. A protocol failure
    (silence, a short reply, a byte that is neither ACK nor NACK) leaves
    the bootloader's parser state unknown: a later address or length frame
    could then land where it expects a command byte, so the session sends
    nothing more and SessionStopped ends it."""
    try:
        value = fn()
    except Refused as e:
        record[key] = {"status": "refused", "stage": e.stage, "detail": str(e)}
        return None
    except ProtocolError as e:
        record[key] = {"status": "failed", "detail": str(e)}
        raise SessionStopped(key) from e
    record[key] = {"status": "ok", "value": value}
    return value


READS = (
    "get",
    "get_version",
    "product_id",
    "flash_size_kbytes",
    "unique_id",
    "option_bytes",
    "cpuid",
    "dbgmcu_idcode",
)


def identify(port):
    """Return (exit status, record) for one session on port."""
    bl = Bootloader(port)
    record = {}
    try:
        bl.sync()
    except ProtocolError as e:
        record["sync"] = {"status": "failed", "detail": str(e)}
        return EXIT_PROTOCOL, record
    record["sync"] = {"status": "ok"}
    try:
        _identify_reads(bl, record)
    except SessionStopped:
        for key in READS:
            record.setdefault(key, {"status": "not run", "detail": "session stopped"})
        return EXIT_PROTOCOL, record

    flash = record["flash_size_kbytes"]
    if record["product_id"]["status"] != "ok":
        return EXIT_PROTOCOL, record
    if flash["status"] == "ok":
        return EXIT_OK, record
    if flash.get("stage") == "command" and record["get"]["status"] == "ok":
        return EXIT_PROTECTED, record
    return EXIT_PROTOCOL, record


MEMORY_READS = ("flash_size_kbytes", "unique_id", "option_bytes", "cpuid", "dbgmcu_idcode")


def _identify_reads(bl, record):
    listed = attempt(record, "get", bl.get)
    attempt(record, "get_version", bl.get_version)
    pid = attempt(record, "product_id", bl.get_id)
    if pid is not None:
        record["product_id"]["hex"] = f"0x{pid:03x}"
        record["product_id"]["meaning"] = PRODUCT_IDS.get(pid, "not in the ST F1 table")
    if listed is None or CMD_READ_MEMORY not in listed["commands"]:
        for key in MEMORY_READS:
            record[key] = {"status": "not run", "detail": "Get did not list Read Memory (0x11)"}
        return
    attempt(
        record,
        "flash_size_kbytes",
        lambda: int.from_bytes(bl.read_memory(FLASH_SIZE_ADDR, 2), "little"),
    )
    attempt(record, "unique_id", lambda: bl.read_memory(UID_ADDR, 12).hex())
    attempt(record, "option_bytes", lambda: bl.read_memory(OPTION_BYTES_ADDR, 16).hex())
    cpuid = attempt(
        record, "cpuid", lambda: int.from_bytes(bl.read_memory(CPUID_ADDR, 4), "little")
    )
    if cpuid is not None:
        record["cpuid"]["hex"] = f"0x{cpuid:08X}"
        record["cpuid"]["meaning"] = CPUID_REVISIONS.get(cpuid, "not r1p1 or r2p1")
    idcode = attempt(
        record,
        "dbgmcu_idcode",
        lambda: int.from_bytes(bl.read_memory(DBGMCU_IDCODE_ADDR, 4), "little"),
    )
    if idcode is not None:
        record["dbgmcu_idcode"]["hex"] = f"0x{idcode:08X}"
        record["dbgmcu_idcode"]["dev_id"] = f"0x{idcode & 0xFFF:03x}"
        record["dbgmcu_idcode"]["rev_id"] = f"0x{idcode >> 16:04x}"


# The STM32F1 family ends at 1 Mbyte of flash (XL density); a larger
# flash-size word is not a value this tool trusts to bound a dump.
MAX_FLASH_KBYTES = 1024


def dump_length_for(record, requested):
    """The dump length in bytes, or None when the request leaves the
    application flash that the flash-size word reports."""
    kbytes = record["flash_size_kbytes"].get("value")
    if not isinstance(kbytes, int) or not 1 <= kbytes <= MAX_FLASH_KBYTES:
        return None
    limit = kbytes * 1024
    length = limit if requested is None else requested
    return length if 1 <= length <= limit else None


def dump_flash(port, length):
    """Read length bytes of application flash; raise on any refusal or
    protocol failure, so a dump never continues on an unknown state."""
    bl = Bootloader(port)
    out = bytearray()
    while len(out) < length:
        n = min(READ_CHUNK, length - len(out))
        out += bl.read_memory(FLASH_BASE + len(out), n)
    return bytes(out)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("tty", help="the CH340C's tty, such as /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--timeout", type=float, default=1.0, help="seconds per read")
    ap.add_argument("--dump", metavar="FILE", help="copy the application flash to FILE")
    ap.add_argument(
        "--dump-length", type=int, help="bytes to copy, 1 to the flash size; default the flash size"
    )
    args = ap.parse_args(argv)
    if args.dump and os.path.lexists(args.dump):
        print(f"kbd_bootloader_id: {args.dump} exists; name a new file", file=sys.stderr)
        return EXIT_USAGE
    try:
        import serial  # pyserial
    except ImportError:
        print("kbd_bootloader_id: pyserial is required (python-pyserial)", file=sys.stderr)
        return EXIT_USAGE
    try:
        port = serial.Serial(
            args.tty,
            args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_EVEN,
            stopbits=serial.STOPBITS_ONE,
            timeout=args.timeout,
        )
    except (serial.SerialException, OSError) as e:
        print(f"kbd_bootloader_id: {args.tty}: {e}", file=sys.stderr)
        return EXIT_PROTOCOL
    with port:
        port.reset_input_buffer()
        status, record = identify(port)
        if args.dump and status == EXIT_OK:
            length = dump_length_for(record, args.dump_length)
            if length is None:
                record["dump"] = {
                    "status": "not run",
                    "detail": "--dump-length outside 1 to the flash size, or no usable flash size",
                }
                status = EXIT_USAGE
            else:
                port.reset_input_buffer()
                try:
                    image = dump_flash(port, length)
                except (Refused, ProtocolError) as e:
                    record["dump"] = {"status": "failed", "detail": str(e)}
                    status = EXIT_PROTOCOL
                else:
                    with open(args.dump, "xb") as fh:
                        fh.write(image)
                    record["dump"] = {
                        "status": "ok",
                        "path": args.dump,
                        "bytes": len(image),
                        "sha256": hashlib.sha256(image).hexdigest(),
                    }
    record["tty"] = args.tty
    record["baud"] = args.baud
    record["read_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    record["exit_status"] = status
    print(json.dumps(record, indent=2))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
