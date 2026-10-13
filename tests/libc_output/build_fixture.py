"""Compile exact stdio output members with an injected write boundary."""

import argparse
import shlex
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("destination", type=Path)
parser.add_argument("--compiler", required=True)
parser.add_argument("--flags", default="")
parser.add_argument("--binary", type=Path)
parser.add_argument("--mutation",
                    choices=("count", "capacity", "pointer", "flush", "continue", "chunk"))
args = parser.parse_args()
args.destination.mkdir(exist_ok=True)
flags = [*shlex.split(args.compiler), *shlex.split(args.flags), "-std=c17",
         "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-nostdinc",
         "-I", str(args.root / "lib/libc/stdio"),
         "-isystem", str(args.root / "include"), "-DNO_DOPRNT_FLOATFMT",
         "-Dfprintf=db_fprintf", "-Dvfprintf=db_vfprintf", "-Dfputs=db_fputs",
         "-D_flsbuf=db_flsbuf", "-Dfflush=db_fflush", "-Dfclose=db_fclose",
         "-Derrno=output_errno", "-Dprintf=db_printf", "-Dvprintf=db_vprintf",
         "-Dsprintf=db_sprintf", "-Dvsprintf=db_vsprintf"]
mutations = {
    "count": ("fprintf", "return result;", "return result == EOF ? EOF : 0;"),
    "capacity": ("vfprintf", "temporary._bufsiz = STDIO_OUTPUT_CHUNK;", "temporary._bufsiz = 0;"),
    "pointer": ("vfprintf", "stream->_cnt = 0;",
                "stream->_cnt = 0;\n\t\tstream->_ptr = output->_ptr;"),
    "flush": ("fputs", "if (result != EOF && fflush(output) == EOF)\n\t\t\tresult = EOF;",
              "if (result != EOF)\n\t\t\t(void)fflush(output);"),
    "continue": ("fputs", "\t\t\tbreak;", "\t\t\tcontinue;"),
    "chunk": ("fputs", "alloca(STDIO_OUTPUT_CHUNK);\n\t\ttemporary._bufsiz = STDIO_OUTPUT_CHUNK;",
              "alloca(BUFSIZ);\n\t\ttemporary._bufsiz = BUFSIZ;"),
}
objects = []
for member in ("fprintf", "vfprintf", "fputs", "doprnt", "doprnt_llong", "flsbuf",
               "printf", "vprintf", "sprintf", "vsprintf"):
    source = args.root / "lib/libc/stdio" / f"{member}.c"
    if args.mutation and mutations[args.mutation][0] == member:
        _, anchor, replacement = mutations[args.mutation]
        contents = source.read_text()
        if contents.count(anchor) != 1:
            raise SystemExit(f"{args.mutation}: mutation anchor differs")
        source = args.destination / f"{member}.mutant.c"
        source.write_text(contents.replace(anchor, replacement))
    output = args.destination / f"{member}.o"
    subprocess.run([*flags, "-Dwrite=output_write", "-Dfstat=output_fstat",
                    "-c", str(source), "-o", str(output)], check=True)
    objects.append(str(output))
subprocess.run([*flags, str(args.root / "tests/libc_output/output_test.c"),
                *objects, "-o", str(args.binary or args.destination / "test")], check=True)
