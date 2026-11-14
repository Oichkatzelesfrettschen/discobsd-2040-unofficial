"""Check which kernels link the SD crash-dump writer.

A dump needs the PicoCalc's SD card, so the PICOCALC kernels link the writer
(sddump in dev/sd.c, dump_write in rp2040/dump.c and dumpsys in
rp2040/machdep.c) and every other kernel links none of it: their dumpdev
stays NODEV and boot() never calls a dump routine (sys/arch/rp2040/include/
dump.h). --with names a kernel that must define all three symbols,
--without one that must define none; the exit status is 1 when any kernel
disagrees and 2 when an image cannot be read.
"""

import argparse
import sys

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

WRITER = ("sddump", "dump_write", "dumpsys")


def defined_writer_symbols(path):
    with open(path, "rb") as stream:
        elf = ELFFile(stream)
        table = elf.get_section_by_name(".symtab")
        if table is None:
            raise ValueError("%s: no symbol table" % path)
        return sorted({sym.name for sym in table.iter_symbols()
                       if sym.name in WRITER and
                       sym["st_shndx"] != "SHN_UNDEF"})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--with", dest="with_", action="append", default=[],
                        metavar="ELF", help="a kernel that links the writer")
    parser.add_argument("--without", action="append", default=[],
                        metavar="ELF", help="a kernel that links none of it")
    args = parser.parse_args(argv)
    if not args.with_ and not args.without:
        parser.error("name at least one kernel")
    failed = False
    for path, want in ([(p, True) for p in args.with_] +
                       [(p, False) for p in args.without]):
        try:
            found = defined_writer_symbols(path)
        except (OSError, ELFError, ValueError) as error:
            print("check-dump-writer-link: %s" % error, file=sys.stderr)
            return 2
        good = len(found) == len(WRITER) if want else not found
        verdict = "PASS" if good else "FAIL"
        print("%s dump-writer-link %s: %s, defines %s" % (
            verdict, path, "writer required" if want else "writer absent",
            " ".join(found) if found else "none"))
        failed = failed or not good
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
