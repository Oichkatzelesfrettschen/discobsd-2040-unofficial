"""Compile and execute the architecture ucall dispatch with controlled state."""

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("sys/arch/rp2040/rp2040/sysctl.c", "sys/arch/stm32/stm32/sysctl.c")
PREFIX = """\
struct user { void *u_arg; int u_rval; } u;
static int permitted, current_priority, selected, restored, called;
static int suser(void) { return permitted; }
static int spl0(void) { selected = 0; return current_priority; }
static int splhigh(void) { selected = 1; return current_priority; }
static void splx(int value) { restored = value; }
static int target(int first, int second) { called++; return first - second; }
"""
SUFFIX = """\
int main(void)
{
    struct args { int priority; int (*routine)(int, int); int arg1, arg2; };
    struct args args = { 0, target, -19, 7 };
    u.u_arg = &args;
    u.u_rval = 42;
    current_priority = 73;
    ucall();
    if (called || u.u_rval != 42) return 1;
    permitted = 1;
    ucall();
    if (called != 1 || u.u_rval != -26 || selected != 0 || restored != 73) return 2;
    args.priority = -3;
    current_priority = 29;
    ucall();
    if (called != 2 || u.u_rval != -26 || selected != 1 || restored != 29) return 3;
    return 0;
}
"""
FLAGS = ("-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wundef", "-Wformat=2")


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix="discobsd-ucall-") as temporary:
        directory = Path(temporary)
        for relative in SOURCES:
            source = (ROOT / relative).read_text(encoding="utf-8")
            start = source.index("void\nucall(void)\n")
            end = source.index("\n/* The ABI reserves ufetch", start)
            body = source[start:end]
            needle = "\tif (!suser())"
            if body.count(needle) != 1:
                failures.append(f"{relative}: dispatch boundary changed")
                continue
            for compiler in ("cc", "clang"):
                executable = shutil.which(compiler)
                if executable is None:
                    failures.append(f"{compiler}: unavailable")
                    continue
                for mutant in ("none", "arity", "permission", "restore"):
                    selected = body
                    if mutant == "arity":
                        selected = body.replace(needle, "\tuap->routine = wrong;\n" + needle)
                    elif mutant == "permission":
                        selected = body.replace(needle, "\tif (0 && !suser())")
                    elif mutant == "restore":
                        if body.count("splx(s);") != 1:
                            failures.append(f"{relative}: priority restore boundary changed")
                            continue
                        selected = body.replace("splx(s);", "splx(s ^ 1);")
                    prefix = PREFIX + ("static int wrong(int value) { return value; }\n"
                                       if mutant == "arity" else "")
                    fixture = directory / "ucall.c"
                    fixture.write_text(prefix + selected + SUFFIX, encoding="utf-8")
                    binary = directory / "ucall"
                    result = subprocess.run([executable, *FLAGS, str(fixture), "-o", str(binary)],
                                            capture_output=True, text=True, check=False)
                    if mutant == "arity":
                        if result.returncode == 0 or "incompatible" not in result.stderr.lower():
                            failures.append(f"{relative} {compiler}: wrong-arity callback accepted")
                    elif result.returncode:
                        failures.append(f"{relative} {compiler}: {result.stderr.strip()}")
                    else:
                        status = subprocess.run([str(binary)], check=False).returncode
                        if (status == 0) != (mutant == "none"):
                            failures.append(f"{relative} {compiler}: {mutant} exit {status}")
    if failures:
        for failure in failures:
            print(f"FAIL ucall-contract: {failure}")
        return 1
    print("PASS ucall-contract: both architectures and host compilers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
