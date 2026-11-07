"""Mutation calibration shared by the games' tests/mutants.py scripts.

A mutant is one textual substitution in a game's source that must match
exactly once. logic() compiles each mutant of the rules half against the
game's host logic test and requires the test to exit nonzero; play()
compiles each mutant of the whole program for the host and requires the
named tests/play.py cases to fail. A mutant that compiles and passes means
the test cannot see that fault, and the run fails.
"""

import os
import shutil
import subprocess
import sys
import tempfile

CC = os.environ.get("HOSTCC", "cc")
STRICT = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-g", "-O1"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]

# A mutant run prints one verdict line per mutant into a pipe that the logged
# gate reads; block buffering would hold minutes of verdicts back and trip the
# gate's idle timeout while the run is still making progress.
sys.stdout.reconfigure(line_buffering=True)


def sanitizers(top):
    """SAN_FLAGS when tools/compiler-probe.sh builds and runs a program with
    them, else [] after a SKIP line: the mutants then run unsanitized."""
    probe = os.path.join(top, "tools", "compiler-probe.sh")
    r = subprocess.run(["sh", probe, "native", CC] + SAN_FLAGS, capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip() == "yes":
        return SAN_FLAGS
    print("SKIP sanitizers: runtime unavailable; mutants run unsanitized")
    return []


def _anchor_ok(name, text, old):
    n = text.count(old)
    if n != 1:
        print("mutant %-34s ERROR: anchor occurs %d times" % (name, n))
        return False
    return True


def logic(src, source, harness, vectors, mutants, top, extra=()):
    """Run the harness on the real source, then on every mutant.

    mutants is a list of (name, old, new). Returns a process exit status.
    """
    text = open(os.path.join(src, source)).read()
    san = sanitizers(top)
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        os.mkdir(os.path.join(work, "tests"))
        shutil.copy(os.path.join(src, "tests", harness), os.path.join(work, "tests", harness))
        exe = os.path.join(work, "logic")

        def build_run():
            cmd = (
                [CC]
                + STRICT
                + san
                + list(extra)
                + ["-o", exe, os.path.join(work, "tests", harness)]
            )
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                return None, "compile error:\n" + r.stderr
            try:
                r = subprocess.run([exe, vectors], capture_output=True, text=True, timeout=300)
            except subprocess.TimeoutExpired:
                return 124, "timeout"
            return r.returncode, r.stdout + r.stderr

        shutil.copy(os.path.join(src, source), os.path.join(work, source))
        rc, out = build_run()
        if rc != 0:
            print("baseline: the real sources do not pass:\n" + out)
            return 1
        print("baseline: real sources pass")
        for name, old, new in mutants:
            if not _anchor_ok(name, text, old):
                failures += 1
                continue
            with open(os.path.join(work, source), "w") as f:
                f.write(text.replace(old, new))
            rc, out = build_run()
            if rc is None:
                print("mutant %-34s ERROR: %s" % (name, out.strip()))
                failures += 1
            elif rc == 0:
                print("mutant %-34s SURVIVED (the test cannot see it)" % name)
                failures += 1
            else:
                first = next(
                    (ln for ln in out.splitlines() if ln.startswith("FAIL") or "Sanitizer" in ln),
                    "exit %d" % rc,
                )
                print("mutant %-34s rejected: %s" % (name, first[:90]))
    n = len(mutants)
    print("logic mutants: %d tried, %d rejected, %d failures" % (n, n - failures, failures))
    return 1 if failures else 0


def play(src, source, binary, mutants, host_flags=()):
    """Run tests/play.py cases against each mutant of the whole program.

    mutants is a list of (name, old, new, cases). Returns an exit status.
    """
    text = open(os.path.join(src, source)).read()
    header = os.path.join(src, "..")
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        proj = os.path.join(work, "g")
        os.makedirs(os.path.join(proj, "x"))
        for h in os.listdir(header):
            if h.endswith(".h"):
                shutil.copy(os.path.join(header, h), os.path.join(proj, h))
        exe = os.path.join(proj, "x", binary)
        script = os.path.join(src, "tests", "play.py")
        for name, old, new, cases in mutants:
            if not _anchor_ok(name, text, old):
                failures += 1
                continue
            path = os.path.join(proj, "x", source)
            with open(path, "w") as f:
                f.write(text.replace(old, new))
            r = subprocess.run(
                [CC, "-std=c17", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-DHOSTBUILD"]
                + list(host_flags)
                + ["-o", exe, path],
                capture_output=True,
                text=True,
            )
            if r.returncode != 0:
                print("play mutant %-30s ERROR: compile\n%s" % (name, r.stderr))
                failures += 1
                continue
            r = subprocess.run(
                [sys.executable, script, exe] + list(cases),
                capture_output=True,
                text=True,
                timeout=300,
            )
            if r.returncode == 0:
                print("play mutant %-30s SURVIVED" % name)
                failures += 1
                continue
            last = (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1]
            print("play mutant %-30s rejected: %s" % (name, last[:90]))
    n = len(mutants)
    print("play mutants: %d tried, %d rejected, %d failures" % (n, n - failures, failures))
    return 1 if failures else 0
