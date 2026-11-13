"""The crash-dump stages of check-picocalc.py, on Renode's SD card model.

check-picocalc.sh makes an 8 MiB card image with tools/fsutil: a UFS file
system on sd0a and a 1 MiB dump partition (MBR type 0xb9) on sd0b. Renode's
SD.SDCard runs it in SPI mode on SPI0 with persistent writes, so the host
reads what the kernel wrote. After the console stages log in as operator:

  reboot -d   su, mount sd0a, make /mnt/crash, reboot -d; the kernel writes
              the dump and resets to a fresh login prompt
  savecore    savecore /mnt/crash saves dump.0 and a second savecore finds
              no dump on the card
  panic       the monitor stops the CPU in the kernel's idle loop and calls
              panic() with a message it wrote past the kernel's RAM; the
              panel shows the panic, the dump line and "halted"

After Renode exits, tools/check_crash_dump.py validates the panic's dump on
the card image and savecore's dump.0, extracted with fsutil, which carries
the message "reboot".

The stages type through the keyboard model in pieces of eight keys and wait
for the model's queue to drain, because its 31-event FIFO drops the keys of
a longer line.
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import check_crash_dump  # noqa: E402
from renode_monitor import monitor_int  # noqa: E402

CARD_BYTES = 8 * 1024 * 1024
COMMAND_BUDGET = 60.0
REBOOT_BUDGET = 150.0
# Short enough that "panic: " and the message fit the panel line the panic
# starts after the shell prompt.
PANIC_MESSAGE = "renode panic"
# A halted kernel spins in cngetc(), which Renode runs about 30 times slower
# than an idle one, so the panic stage's budget is short.
PANIC_BUDGET = 10.0
KERNEL_FLASH = (0x10000100, 0x10020000)
CHUNK = 8


def elf_symbols(path, names):
    """Values of the named symbols in an ELF32 little-endian image."""
    with open(path, "rb") as stream:
        data = stream.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError(f"{path}: not a little-endian ELF32 image")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
                for i in range(shnum)]
    found = {}
    for sh in sections:
        if sh[1] != 2:          # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for offset in range(sh[4], sh[4] + sh[5], 16):
            name_off, value, size, _info, _other, _shndx = struct.unpack_from(
                "<IIIBBH", data, offset)
            start = strtab[4] + name_off
            name = data[start:data.index(b"\0", start)].decode()
            if name in names:
                found[name] = (value, size)
    missing = [name for name in names if name not in found]
    if missing:
        raise ValueError(f"{path}: no symbol {', '.join(missing)}")
    return found


def make_card(path, fsutil):
    """The card image: UFS on partition 1, the dump partition on 2."""
    with open(path, "wb") as stream:
        stream.truncate(CARD_BYTES)
    for args in (["--repartition=fs=4m:dump=1m", path],
                 ["--new", "--partition=1", path]):
        subprocess.run([fsutil, *args], check=True, stdout=subprocess.DEVNULL)


def attach_card(renode, path):
    renode.command(
        'machine LoadPlatformDescriptionFromString "sd: SD.SDCard @ spi0 '
        f'{{ imageFile: \\"{path}\\"; capacity: {CARD_BYTES}; '
        'persistent: true; spiMode: true }"')


class Console:
    """Typing and waiting for one command at a time on the panel console."""

    def __init__(self, run):
        self.run = run
        self.serial = 0

    def drained(self):
        kbd = "sysbus.i2c1.kbd"
        return (monitor_int(self.run.renode, f"{kbd} Delivered") ==
                monitor_int(self.run.renode, f"{kbd} Queued"))

    def type(self, text):
        renode = self.run.renode
        for start in range(0, len(text), CHUNK):
            renode.command(f'sysbus.i2c1.kbd TypeText "{text[start:start + CHUNK]}"')
            for _ in range(60):
                self.run.advance()
                if self.drained():
                    break
        renode.command("sysbus.i2c1.kbd TypeKey 10")

    def command(self, text, what):
        """Run text, then wait for the line its marker echo prints."""
        self.serial += 1
        mark = f"dm{self.serial}"
        self.type(f"{text};echo {mark}")
        self.run.wait_for_line(mark, what, COMMAND_BUDGET)

    def login_root(self, what):
        self.type("operator")
        self.run.wait_for_prompt(f"{what}: operator shell", COMMAND_BUDGET)
        self.type("su")
        self.run.wait_until(
            lambda: any(line.rstrip().endswith("#") for line in self.run.lines),
            f"{what}: root shell", "a # prompt", COMMAND_BUDGET)


def check_card(run, card, message):
    """Validate the dump on the card image against message."""
    with open(card, "rb") as stream:
        data = stream.read()
    start, length = check_crash_dump.dump_partition(data)
    failures, segments = check_crash_dump.check_dump(data[start:start + length], message)
    for failure in failures:
        run.fail(f"dump ({message}) on the card: {failure}")
    if not failures:
        print("check-picocalc: dump on the card: " + ", ".join(
            f"{check_crash_dump.KINDS[kind]} {size} bytes"
            for kind, _a, size, _d in segments))


def card_has_dump(card):
    """Whether the host file shows a header on the dump partition now."""
    with open(card, "rb") as stream:
        data = stream.read()
    start, _length = check_crash_dump.dump_partition(data)
    return data[start:start + 4] == struct.pack("<I", check_crash_dump.MAGIC)


def extract_file(card, fsutil, name):
    """A file's bytes from the card's UFS partition, or None."""
    with tempfile.TemporaryDirectory() as work:
        subprocess.run([fsutil, "--extract", "--partition=1", card], cwd=work,
                       check=True, stdout=subprocess.DEVNULL)
        path = os.path.join(work, name)
        if not os.path.exists(path):
            return None
        with open(path, "rb") as stream:
            return stream.read()


def inject_panic(run, elf):
    """Stop the CPU in idle() and enter panic(PANIC_MESSAGE) from there.

    idle() runs privileged with nothing locked, the state a kernel waiting
    for console input is in. The message goes into the free kernel RAM past
    __kernel_ram_used_end, outside the span the dump copies.
    """
    renode = run.renode
    symbols = elf_symbols(elf, ["panic", "idle", "__kernel_ram_used_end"])
    idle, idle_size = symbols["idle"]
    idle &= ~1
    for _ in range(2000):
        pc = monitor_int(renode, "sysbus.cpu0 PC")
        if idle <= pc < idle + idle_size:
            break
        renode.command('emulation RunFor "0.0005"')
    else:
        run.fail("panic: the CPU never stopped in idle()")
        return False
    text = symbols["__kernel_ram_used_end"][0] + 256
    for index, byte in enumerate(PANIC_MESSAGE.encode() + b"\0"):
        renode.command(f"sysbus WriteByte {text + index:#x} {byte:#x}")
    renode.command(f"sysbus.cpu0 SetRegister 0 {text:#x}")
    renode.command(f"sysbus.cpu0 PC {symbols['panic'][0] & ~1:#x}")
    print(f"check-picocalc: panic entered from idle() at {pc:#x}")
    return True


def run_dump_stages(run, card, elf):
    """The emulated stages; check_after_close() reads the card afterwards.

    Renode's SD card model writes the image through a buffered stream, so the
    host file can lag the last sectors the kernel wrote until the emulator
    exits; the file is read for assertions only after close, and the reads
    during the run are printed as observations.
    """
    console = Console(run)
    console.login_root("reboot -d")
    console.command("mount /dev/sd0a /mnt;mkdir /mnt/crash", "mount sd0a")
    console.type("reboot -d")
    # The reset clears the panel, so a login prompt without the command
    # line above it is the next boot's.
    run.wait_until(lambda: run.has("login:") and not run.has("reboot -d"),
                   "login after reboot -d", "a fresh login prompt", REBOOT_BUDGET)
    print("check-picocalc: observation: the host file shows the dump header "
          f"after the reboot: {card_has_dump(card)}")

    console.login_root("savecore")
    console.command("mount /dev/sd0a /mnt", "mount sd0a again")
    console.type("savecore /mnt/crash")
    run.wait_for(["savecore: /mnt/crash/dump.0: reboot"], "savecore saves dump.0",
                 COMMAND_BUDGET)
    console.type("savecore -v /mnt/crash")
    run.wait_for(["savecore: no dump on /dev/rsd0b"], "savecore finds no dump",
                 COMMAND_BUDGET)
    console.command("umount /mnt", "unmount sd0a")
    run.snapshot("savecore")

    if inject_panic(run, elf):
        run.wait_for(["panic: " + PANIC_MESSAGE, "dump: sd0b, 1024 kbytes: succeeded",
                      "halted"], "panic dump", PANIC_BUDGET)
        run.snapshot("panic")


def check_after_close(run, card, fsutil):
    """The card as Renode left it: the panic's dump, and savecore's dump.0."""
    check_card(run, card, PANIC_MESSAGE)
    saved = extract_file(card, fsutil, "crash/dump.0")
    if saved is None:
        run.fail("savecore: no crash/dump.0 on the card's file system")
        return
    failures, segments = check_crash_dump.check_dump(saved, "reboot")
    for failure in failures:
        run.fail(f"savecore: dump.0: {failure}")
    if not failures:
        print(f"check-picocalc: dump.0 ({len(saved)} bytes) holds a valid dump of "
              f"{len(segments)} segments with the message 'reboot'")
