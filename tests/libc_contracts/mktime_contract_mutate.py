"""Prove the mktime contract rejects the two range-edge defects of mktime().

usage: mktime_contract_mutate.py ROOT COMPILE TEST OBJECT...

COMPILE is the compiler command and flags the gate builds mktime.c with,
TEST the contract test source and each OBJECT a member it links beside
mktime.c (ctime.c, tzset.c and timezone.c). The unchanged source must pass
and each mutant of lib/libc/gen/mktime.c must fail:

 - preflight: the fields read as UTC are refused outside the range before
   the zone offset is applied, which refuses the first hours of the range
   in a zone west of Greenwich and the last hours in a zone east of it;
 - value-only: an answer equal to TIME_REFUSED is taken as a refusal, which
   refuses the last count of the range, 4294967295, and leaves the struct.
"""

import pathlib
import shlex
import subprocess
import sys
import tempfile

PREFLIGHT_ANCHOR = "\t\tguess = days < 0 ? TIME_MIN : TIME_MAX;\n"
VALUE_ANCHOR = "\t*tm = *localtime(&guess);\n"

MUTANTS = (
    ("preflight", PREFLIGHT_ANCHOR, "\t\treturn (TIME_REFUSED);\n"),
    ("value-only", VALUE_ANCHOR,
     "\tif (guess == TIME_REFUSED)\n\t\treturn (TIME_REFUSED);\n"
     + VALUE_ANCHOR),
)


def require(condition, message):
    if not condition:
        raise SystemExit("mktime mutants: " + message)


def build_and_run(compile_command, source, test, objects, directory, label):
    program = directory / ("mktime_" + label)
    build = subprocess.run(
        compile_command + ["-o", str(program), str(test), str(source)]
        + [str(item) for item in objects],
        capture_output=True, text=True, check=False)
    require(build.returncode == 0,
            label + " did not build:\n" + build.stdout + build.stderr)
    run = subprocess.run([str(program)], capture_output=True, text=True,
                         check=False, timeout=600)
    return run.returncode


def main():
    require(len(sys.argv) >= 4, "usage: mktime_contract_mutate.py ROOT "
            "COMPILE TEST OBJECT...")
    root = pathlib.Path(sys.argv[1]).resolve()
    # The copies live outside lib/libc/gen, which holds offtime.h.
    compile_command = shlex.split(sys.argv[2]) + [
        "-I" + str(root / "lib/libc/gen")]
    test = pathlib.Path(sys.argv[3]).resolve()
    objects = [pathlib.Path(item).resolve() for item in sys.argv[4:]]
    original = (root / "lib/libc/gen/mktime.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="mktime-mutants-") as name:
        directory = pathlib.Path(name)
        baseline = directory / "mktime.c"
        baseline.write_text(original, encoding="utf-8")
        require(build_and_run(compile_command, baseline, test, objects,
                              directory, "baseline") == 0,
                "the unchanged mktime.c failed the contract")
        print("mktime mutants: unchanged source passes")
        for label, anchor, replacement in MUTANTS:
            require(original.count(anchor) == 1, label + " anchor differs")
            mutant = directory / ("mktime_" + label + ".c")
            mutant.write_text(original.replace(anchor, replacement),
                              encoding="utf-8")
            status = build_and_run(compile_command, mutant, test, objects,
                                   directory, label)
            require(status != 0, label + " mutant passed the contract")
            print("mktime mutants: %s mutant rejected" % label)


if __name__ == "__main__":
    main()
