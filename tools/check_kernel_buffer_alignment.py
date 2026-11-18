"""Check the link address of kernel buffers that hold structures.

The buffer cache's memory (bufdata, machdep.c) is declared as a character
array, and file-system code reads superblocks, inodes and directory entries
out of it with word loads. A Cortex-M0+ raises HardFault on an unaligned
word access, and an emulator that permits one boots a kernel the board
cannot, so the address is checked on the linked image. Each --symbol
NAME:ALIGN must be defined in every named unix.elf at a multiple of ALIGN;
the exit status is 1 when one is not and 2 when an image cannot be read or
lacks the symbol.
"""

import argparse
import sys

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile


def symbol_addresses(path, names):
    with open(path, "rb") as stream:
        elf = ELFFile(stream)
        table = elf.get_section_by_name(".symtab")
        if table is None:
            raise ValueError("%s: no symbol table" % path)
        found = {}
        for sym in table.iter_symbols():
            if sym.name in names and sym["st_shndx"] != "SHN_UNDEF":
                found[sym.name] = sym["st_value"]
        return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--symbol", action="append", default=[],
                        metavar="NAME:ALIGN", required=True)
    parser.add_argument("elf", nargs="+")
    args = parser.parse_args(argv)
    wanted = {}
    for item in args.symbol:
        name, _, align = item.partition(":")
        if not name or not align.isdigit() or int(align) < 1:
            parser.error("--symbol takes NAME:ALIGN, got %r" % item)
        wanted[name] = int(align)
    failed = False
    for path in args.elf:
        try:
            found = symbol_addresses(path, wanted)
        except (OSError, ELFError, ValueError) as error:
            print("check-kernel-buffer-alignment: %s" % error, file=sys.stderr)
            return 2
        for name, align in sorted(wanted.items()):
            if name not in found:
                print("check-kernel-buffer-alignment: %s does not define %s"
                      % (path, name), file=sys.stderr)
                return 2
            good = found[name] % align == 0
            print("%s buffer-alignment %s: %s at 0x%08x, required multiple of %d"
                  % ("PASS" if good else "FAIL", path, name, found[name],
                     align))
            failed = failed or not good
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
