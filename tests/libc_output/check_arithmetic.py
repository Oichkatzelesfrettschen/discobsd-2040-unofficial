"""Exercise formatter arithmetic under sanitizers and calibrate digit parsing."""

import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2])
source_directory = root / "lib/libc/stdio"
fixture = root / "tests/libc_contracts/printf_contract_test.c"
flags = ["-std=c17", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-nostdinc",
         "-isystem", str(root / "include"), "-Derrno=printf_test_errno",
         "-Dsnprintf=arithmetic_snprintf", "-Dvsnprintf=arithmetic_vsnprintf",
         "-D_flsbuf=arithmetic_flsbuf", "-Dfflush=arithmetic_fflush",
         "-Dfclose=arithmetic_fclose", "-fno-sanitize-recover=all",
         "-fno-omit-frame-pointer"]
environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1")
with tempfile.TemporaryDirectory(prefix="printf-arithmetic-") as directory:
    work = Path(directory)
    for floating, mutation in ((False, False), (True, False), (False, True)):
        formatter = source_directory / "doprnt.c"
        if mutation:
            contents = formatter.read_text()
            anchor = "n = n * 10 + (c - '0');"
            if contents.count(anchor) != 1:
                raise SystemExit("digit grouping mutation anchor differs")
            formatter = work / "doprnt-mutant.c"
            formatter.write_text(contents.replace(anchor, "n = n * 10 + c - '0';"))
        extra = ["-DPRINTF_FLOAT_TEST"] if floating else ["-DNO_DOPRNT_FLOATFMT"]
        sources = [str(source_directory / (name + ".c"))
                   for name in ("snprintf", "vsnprintf", "flsbuf", "doprnt_llong")]
        if floating:
            sources.append(str(source_directory / "doprnt_float.c"))
        binary = work / ("mutant" if mutation else "float" if floating else "integer")
        sanitizer = "undefined" if mutation else "address,undefined"
        subprocess.run([*compiler, *flags, "-fsanitize=" + sanitizer, *extra,
                        str(fixture), str(formatter), *sources, "-lm", "-o", str(binary)],
                       check=True)
        result = subprocess.run([str(binary)], env=environment, capture_output=True,
                                text=True, timeout=15)
        if mutation:
            if result.returncode != 1 or "signed integer overflow" not in result.stderr:
                raise SystemExit("digit grouping control misses its expected sanitizer check")
            print("PASS decimal intermediate-overflow mutation rejected")
        elif result.returncode != 0:
            raise SystemExit(result.stdout + result.stderr)
        else:
            print("PASS sanitized formatter " + ("float" if floating else "integer"))
