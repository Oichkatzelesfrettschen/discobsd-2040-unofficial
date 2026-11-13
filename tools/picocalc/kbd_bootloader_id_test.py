"""Calibration for tools/picocalc/kbd_bootloader_id.py against a model of
the STM32 USART bootloader (AN3155 framing).

The model checks every complement and address checksum, answers from a
memory map, and can refuse the private peripheral bus or one address,
apply readout protection (a NACK of the Read Memory command byte), stay
silent, answer garbage or cut a reply short. The client must exit 0 on a
readable part, 3 only when the command byte is refused, 1 on every other
refusal of the flash-size read and on every protocol failure, after which
it sends nothing more; it must decode the product ID, CPUID and
DBGMCU_IDCODE fields, bound a dump to the reported flash size, open the
port at 57600 baud 8E1, and never put a byte outside its read-only
command set on the wire. Three mutant clients (a little-endian address, a
missing address checksum, a length byte sent without its complement) must
exit 1 without claiming readout protection, and a client that swaps
DEV_ID and REV_ID must be caught.
"""

import contextlib
import hashlib
import io
import json
import os
import sys
import tempfile
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kbd_bootloader_id as kbi  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


UID = bytes(range(0x31, 0x3D))
MEMORY = {
    kbi.FLASH_SIZE_ADDR: (128).to_bytes(2, "little"),
    kbi.UID_ADDR: UID,
    kbi.OPTION_BYTES_ADDR: bytes([0xA5, 0x5A, 0xFF, 0x00] + [0xFF, 0x00] * 6),
    kbi.CPUID_ADDR: (0x411FC231).to_bytes(4, "little"),
    kbi.DBGMCU_IDCODE_ADDR: bytes(4),
}
FIRMWARE = bytes((i * 7 + 3) & 0xFF for i in range(1000))
for off in range(0, len(FIRMWARE), kbi.READ_CHUNK):
    MEMORY[kbi.FLASH_BASE + off] = FIRMWARE[off : off + kbi.READ_CHUNK]


class ModelBootloader:
    """A byte-level bootloader: write() feeds the parser, read() drains replies."""

    def __init__(
        self,
        protected=False,
        refuse_ppb=True,
        silent=False,
        garbage=False,
        short_read=False,
        pid=0x410,
        already_synced=False,
        memory=None,
        refuse=(),
    ):
        self.already_synced = already_synced
        self.protected = protected
        self.refuse_ppb = refuse_ppb
        self.silent = silent
        self.garbage = garbage
        self.short_read = short_read
        self.pid = pid
        self.memory = dict(MEMORY)
        self.memory.update(memory or {})
        self.refuse = set(refuse)
        self.out = bytearray()
        self.inbuf = bytearray()
        self.state = "idle"
        self.addr = 0
        self.commands = []
        self.addresses = []

    def _reply(self, *bs):
        self.out.extend(bs)

    def write(self, data):
        self.inbuf.extend(data)
        self._parse()

    def read(self, n):
        chunk = bytes(self.out[:n])
        del self.out[:n]
        return chunk

    def reset_input_buffer(self):
        self.out.clear()

    # serial.Serial is its own context manager; main() uses the model as one.
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def _parse(self):
        while True:
            if self.state == "idle":
                if not self.inbuf:
                    return
                if self.inbuf[0] == kbi.SYNC and not self.already_synced:
                    del self.inbuf[0]
                    if self.garbage:
                        self._reply(0x00)
                    elif not self.silent:
                        self._reply(kbi.ACK)
                    continue
                if len(self.inbuf) < 2:
                    return
                cmd, comp = self.inbuf[0], self.inbuf[1]
                del self.inbuf[:2]
                self.commands.append(cmd)
                if cmd ^ comp != 0xFF:
                    self._reply(kbi.NACK)
                    continue
                self._command(cmd)
            elif self.state == "addr":
                if len(self.inbuf) < 5:
                    return
                a = bytes(self.inbuf[:4])
                xor = self.inbuf[4]
                del self.inbuf[:5]
                addr = int.from_bytes(a, "big")
                self.addresses.append(addr)
                ok = a[0] ^ a[1] ^ a[2] ^ a[3] == xor and addr in self.memory
                if ok and (addr in self.refuse or (self.refuse_ppb and addr >= 0xE0000000)):
                    ok = False
                self._reply(kbi.ACK if ok else kbi.NACK)
                self.addr = addr
                self.state = "len" if ok else "idle"
            elif self.state == "len":
                if len(self.inbuf) < 2:
                    return
                n, comp = self.inbuf[0], self.inbuf[1]
                del self.inbuf[:2]
                self.state = "idle"
                if n ^ comp != 0xFF:
                    self._reply(kbi.NACK)
                    continue
                self._reply(kbi.ACK)
                data = self.memory[self.addr][: n + 1]
                if self.short_read:
                    data = data[:-1]
                self.out.extend(data)

    def _command(self, cmd):
        if cmd == kbi.CMD_GET:
            cmds = [0x00, 0x01, 0x02, 0x11, 0x21, 0x31, 0x43, 0x63, 0x73, 0x82, 0x92]
            self._reply(kbi.ACK, len(cmds), 0x22, *cmds, kbi.ACK)
        elif cmd == kbi.CMD_GET_VERSION:
            self._reply(kbi.ACK, 0x22, 0x00, 0x00, kbi.ACK)
        elif cmd == kbi.CMD_GET_ID:
            self._reply(kbi.ACK, 1, self.pid >> 8, self.pid & 0xFF, kbi.ACK)
        elif cmd == kbi.CMD_READ_MEMORY:
            if self.protected:
                self._reply(kbi.NACK)
            else:
                self._reply(kbi.ACK)
                self.state = "addr"
        else:
            self._reply(kbi.NACK)


def run(**kw):
    model = ModelBootloader(**kw)
    status, record = kbi.identify(model)
    return status, record, model


# A readable medium-density part whose bootloader refuses the PPB.
status, rec, model = run()
check(status == kbi.EXIT_OK, "readable part exits 0")
check(rec["product_id"]["hex"] == "0x410", "product ID 0x410 decodes")
check(rec["product_id"]["meaning"].startswith("medium"), "0x410 reads medium density")
check(rec["flash_size_kbytes"]["value"] == 128, "flash-size word reads 128 Kbytes")
check(rec["unique_id"]["value"] == UID.hex(), "unique ID reads 12 bytes in address order")
check(rec["option_bytes"]["value"].startswith("a55a"), "option bytes read RDP 0xA5 first")
check(rec["get"]["value"]["version"] == 0x22, "Get reports bootloader version 0x22")
check(
    rec["get_version"]["value"] == {"version": 0x22, "option_bytes": [0, 0]},
    "Get Version reports 0x22 and two option bytes",
)
check(
    rec["cpuid"]["status"] == "refused" and rec["cpuid"]["stage"] == "address",
    "a refused CPUID address records refused at the address stage",
)
check(set(model.commands) <= kbi.ALLOWED_COMMANDS, "only read-only commands reach the wire")

# A high-density r2p1 part whose bootloader serves the PPB and whose
# DBGMCU_IDCODE reads nonzero: every decoded field must follow the value.
IDCODE_NONZERO = {kbi.DBGMCU_IDCODE_ADDR: (0x20036414).to_bytes(4, "little")}
status, rec, model = run(
    refuse_ppb=False,
    pid=0x414,
    memory={kbi.CPUID_ADDR: (0x412FC231).to_bytes(4, "little"), **IDCODE_NONZERO},
)
check(status == kbi.EXIT_OK, "PPB-serving part exits 0")
check(rec["product_id"]["meaning"].startswith("high"), "0x414 reads high density")
check(rec["cpuid"].get("meaning") == "Cortex-M3 r2p1", "CPUID 0x412FC231 decodes r2p1")
check(
    rec["dbgmcu_idcode"].get("dev_id") == "0x414"
    and rec["dbgmcu_idcode"].get("rev_id") == "0x2003",
    "DBGMCU_IDCODE 0x20036414 decodes DEV_ID 0x414, REV_ID 0x2003",
)
status, rec, model = run(refuse_ppb=False)
check(rec["cpuid"].get("meaning") == "Cortex-M3 r1p1", "CPUID 0x411FC231 decodes r1p1")
check(rec["dbgmcu_idcode"].get("hex") == "0x00000000", "zero DBGMCU_IDCODE is recorded as read")

# Readout protection refuses the Read Memory command byte.
status, rec, model = run(protected=True)
check(status == kbi.EXIT_PROTECTED, "readout-protected part exits 3")
check(rec["flash_size_kbytes"]["stage"] == "command", "protection refuses at the command stage")
check(rec["product_id"]["status"] == "ok", "Get ID still answers under protection")

# A refused flash-size address is not readout protection.
status, rec, model = run(refuse={kbi.FLASH_SIZE_ADDR})
check(status == kbi.EXIT_PROTOCOL, "a refused flash-size address exits 1, not 3")

# A bootloader synced by an earlier session takes 0x7F as a command byte.
status, rec, model = run(already_synced=True)
check(status == kbi.EXIT_OK, "already-synced bootloader exits 0 after the second 0x7F")
check(model.commands[0] == kbi.SYNC, "the paired 0x7F bytes drew the NACK")

# Failures of the link.
status, rec, _ = run(silent=True)
check(status == kbi.EXIT_PROTOCOL and rec["sync"]["status"] == "failed", "silent port exits 1")
status, rec, _ = run(garbage=True)
check(status == kbi.EXIT_PROTOCOL, "garbage sync reply exits 1")
status, rec, model = run(short_read=True)
check(status == kbi.EXIT_PROTOCOL, "short Read Memory reply exits 1")
check(rec["flash_size_kbytes"]["status"] == "failed", "short reply records failed, not refused")
check(
    model.addresses == [kbi.FLASH_SIZE_ADDR] and rec["unique_id"]["status"] == "not run",
    "after a protocol failure the session sends no further frame",
)

# Read Memory lengths stay inside one request.
for bad in (0, 257):
    try:
        kbi.Bootloader(ModelBootloader()).read_memory(kbi.FLASH_BASE, bad)
        bounded = False
    except ValueError:
        bounded = True
    check(bounded, f"read length {bad} is refused before any byte is written")

# The command allow-list refuses every writing or executing command.
for cmd in (0x21, 0x31, 0x43, 0x44, 0x63, 0x73, 0x82, 0x92):
    model = ModelBootloader()
    try:
        kbi.Bootloader(model).send_command(cmd)
        refused = False
    except ValueError:
        refused = True
    check(
        refused and not model.inbuf and not model.commands,
        f"command 0x{cmd:02x} is refused before any byte is written",
    )

# The flash dump reads 256-byte chunks in address order and stops at length.
model = ModelBootloader()
kbi.Bootloader(model).sync()
image = kbi.dump_flash(model, len(FIRMWARE))
check(image == FIRMWARE, "dump of 1000 bytes equals the model's flash, across four chunks")
check(set(model.commands) <= kbi.ALLOWED_COMMANDS, "the dump sends only Read Memory")
model = ModelBootloader(protected=True)
try:
    kbi.dump_flash(model, 16)
    dump_refused = False
except kbi.Refused:
    dump_refused = True
check(dump_refused, "a readout-protected dump raises Refused")

# The dump length stays inside the reported flash.
flash_rec = {"flash_size_kbytes": {"status": "ok", "value": 64}}
check(kbi.dump_length_for(flash_rec, None) == 65536, "the default dump length is the flash size")
for bad in (0, -1, 65537, 2_220_000):
    check(kbi.dump_length_for(flash_rec, bad) is None, f"--dump-length {bad} is refused")
check(
    kbi.dump_length_for({"flash_size_kbytes": {"status": "ok", "value": 0xFFFF}}, None) is None,
    "a flash-size word above 1 Mbyte bounds no dump",
)


class FakeSerialModule(types.ModuleType):
    """The part of pyserial main() uses; it records each open."""

    EIGHTBITS, PARITY_EVEN, PARITY_NONE, STOPBITS_ONE = 8, "E", "N", 1

    class SerialException(Exception):
        pass

    def __init__(self):
        super().__init__("serial")
        self.opened = []

    def Serial(self, tty, baud, **kw):  # noqa: N802 (pyserial's name)
        model = ModelBootloader()
        self.opened.append((tty, baud, kw, model))
        return model


def run_main(argv):
    fake = FakeSerialModule()
    saved = sys.modules.get("serial")
    sys.modules["serial"] = fake
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            status = kbi.main(argv)
    finally:
        if saved is None:
            del sys.modules["serial"]
        else:
            sys.modules["serial"] = saved
    text = out.getvalue()
    return status, (json.loads(text) if text else {}), fake


# main() opens the port at 57600 8E1 and writes the dump with its digest.
with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, "kbd.bin")
    status, rec, fake = run_main(["/dev/fake", "--dump", path, "--dump-length", "1000"])
    tty, baud, kw, model = fake.opened[0]
    check(status == kbi.EXIT_OK and rec["exit_status"] == 0, "main exits 0 and records it")
    check(
        baud == 57600 and kw.get("parity") == "E" and kw.get("bytesize") == 8,
        "main opens the port at 57600 baud, 8 data bits, even parity",
    )
    check(kw.get("stopbits") == 1, "main opens the port with one stop bit")
    with open(path, "rb") as fh:
        written = fh.read()
    check(written == FIRMWARE, "main writes the dumped bytes")
    check(
        rec["dump"]["sha256"] == hashlib.sha256(FIRMWARE).hexdigest(),
        "main records the dump's sha256",
    )
    big = os.path.join(tmp, "big.bin")
    status, rec, fake = run_main(["/dev/fake", "--dump", big, "--dump-length", "2220000"])
    model = fake.opened[0][3]
    check(
        status == kbi.EXIT_USAGE and not os.path.exists(big),
        "a dump longer than the flash exits 2 and writes no file",
    )
    check(
        not any(kbi.FLASH_BASE <= a < kbi.FLASH_SIZE_ADDR for a in model.addresses),
        "a refused dump length sends no flash address",
    )


# Mutant clients must fail.
class LittleEndianAddress(kbi.Bootloader):
    def read_memory(self, addr, length):
        self.send_command(kbi.CMD_READ_MEMORY)
        a = addr.to_bytes(4, "little")
        self.port.write(a + bytes([a[0] ^ a[1] ^ a[2] ^ a[3]]))
        self._expect_ack("addr", "address")
        self.port.write(bytes([length - 1, (length - 1) ^ 0xFF]))
        self._expect_ack("len", "length")
        return self._read_exact(length, "data")


class NoChecksum(kbi.Bootloader):
    def read_memory(self, addr, length):
        self.send_command(kbi.CMD_READ_MEMORY)
        self.port.write(addr.to_bytes(4, "big") + b"\x00")
        self._expect_ack("addr", "address")
        self.port.write(bytes([length - 1, (length - 1) ^ 0xFF]))
        self._expect_ack("len", "length")
        return self._read_exact(length, "data")


class NoLengthComplement(kbi.Bootloader):
    def read_memory(self, addr, length):
        self.send_command(kbi.CMD_READ_MEMORY)
        a = addr.to_bytes(4, "big")
        self.port.write(a + bytes([a[0] ^ a[1] ^ a[2] ^ a[3]]))
        self._expect_ack("addr", "address")
        self.port.write(bytes([length - 1, 0x00]))
        self._expect_ack("len", "length")
        return self._read_exact(length, "data")


shipped = kbi.Bootloader
for mutant in (LittleEndianAddress, NoChecksum, NoLengthComplement):
    kbi.Bootloader = mutant
    try:
        status, rec, _ = run()
    finally:
        kbi.Bootloader = shipped
    check(status == kbi.EXIT_PROTOCOL, f"mutant {mutant.__name__} exits 1, claiming no protection")

shipped_identify_reads = kbi._identify_reads


def swapped_idcode(bl, record):
    shipped_identify_reads(bl, record)
    entry = record["dbgmcu_idcode"]
    if entry["status"] == "ok":
        entry["dev_id"], entry["rev_id"] = entry["rev_id"], entry["dev_id"]


kbi._identify_reads = swapped_idcode
try:
    status, rec, _ = run(refuse_ppb=False, memory=IDCODE_NONZERO)
finally:
    kbi._identify_reads = shipped_identify_reads
check(rec["dbgmcu_idcode"]["dev_id"] != "0x414", "mutant that swaps DEV_ID and REV_ID is caught")

print(f"kbd_bootloader_id: {failures} failed")
sys.exit(1 if failures else 0)
